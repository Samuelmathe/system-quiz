#!/usr/bin/env python3
"""
Quiz Logger - Interface graphique
----------------------------------
Écoute PASSIVE sur un port série de diagnostic (adaptateur USB-TTL dédié,
branché en parallèle sur PC_SERIAL / Serial3 du Mega — aucun conflit avec
le logiciel de score, qui écoute son propre adaptateur séparément).

Affiche le flux en direct, enregistre le log complet dans un fichier
horodaté, ET génère un petit fichier "résumé" (à la fermeture ou sur
demande) facile à envoyer pour le diagnostic : durée de session, nombre
de buzz, freezes/silences détectés, redémarrages Mega détectés.

Prérequis :
    pip install pyserial
    (tkinter fait partie de la bibliothèque standard Python ; sur Linux,
    si "import tkinter" échoue : sudo apt install python3-tk)
"""

import os
import sys
import re
import time
import queue
import platform
import subprocess
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Il manque pyserial. Installe-le avec : pip install pyserial")
    sys.exit(1)

BAUDRATE = 9600
GAP_ALERT_SECONDS = 5.0   # aucune ligne reçue pendant ce délai -> alerte freeze possible
POLL_MS = 100              # fréquence de traitement de la file d'attente (ms)
BANNER_REFRESH_MS = 500    # fréquence de rafraîchissement du bandeau d'état (ms)

ALIVE_RE = re.compile(r"ALIVE:(\d+),radioOK=(\d),locked=(\d)")


class QuizLoggerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Quiz Logger - Diagnostic Buzzer/DMX")
        self.root.geometry("800x580")
        self.root.minsize(620, 420)

        self.ser = None
        self.reading = False
        self.line_queue = queue.Queue()

        self.dernier_millis_mega = None
        self.dernier_radio_ok = None
        self.derniere_ligne_ts = time.monotonic()
        self._alerte_active = False
        self._ports_disponibles = []
        self.port_utilise = None

        # Statistiques de session (pour le résumé)
        self.session_start = datetime.now()
        self.stat_total_lignes = 0
        self.stat_nb_buzz = 0
        self.stat_nb_ignored = 0
        self.stat_nb_radio_down = 0
        self.silences = []   # liste de (horodatage_str, duree_s)
        self.reboots = []    # liste de (horodatage_str, millis_avant, millis_apres)

        self._build_ui()
        self._ouvrir_fichier_log()
        self._scanner_ports(auto_connect=True)

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(POLL_MS, self._poll_queue)
        self.root.after(BANNER_REFRESH_MS, self._refresh_banner)

    # ================= UI =================
    def _build_ui(self):
        top = ttk.Frame(self.root, padding=8)
        top.pack(fill="x")

        ttk.Label(top, text="Port :").pack(side="left")
        self.combo_ports = ttk.Combobox(top, state="readonly", width=38)
        self.combo_ports.pack(side="left", padx=6)

        ttk.Button(top, text="Actualiser",
                   command=lambda: self._scanner_ports(auto_connect=False)).pack(side="left", padx=4)
        self.btn_connect = ttk.Button(top, text="Connecter", command=self._toggle_connexion)
        self.btn_connect.pack(side="left", padx=4)

        self.lbl_status = ttk.Label(top, text="Déconnecté", foreground="#b00000")
        self.lbl_status.pack(side="left", padx=10)

        # Bandeau d'état : gros indicateur visuel, lisible de loin pendant un show
        self.lbl_banner = tk.Label(
            self.root, text="EN ATTENTE DE CONNEXION",
            font=("Sans", 16, "bold"), bg="#444444", fg="white", pady=10
        )
        self.lbl_banner.pack(fill="x")

        # Compteurs en direct
        stats_frame = ttk.Frame(self.root, padding=(8, 4))
        stats_frame.pack(fill="x")
        self.lbl_stats = ttk.Label(
            stats_frame,
            text="Buzz: 0   |   Ignorés: 0   |   Silences: 0   |   Redémarrages: 0",
            foreground="#aaaaaa"
        )
        self.lbl_stats.pack(side="left")

        # Zone de log
        self.txt_log = scrolledtext.ScrolledText(
            self.root, state="disabled", wrap="word",
            font=("Monospace", 9), bg="#111111", fg="#dddddd", insertbackground="white"
        )
        self.txt_log.pack(fill="both", expand=True, padx=8, pady=8)

        self.txt_log.tag_config("alive", foreground="#777777")
        self.txt_log.tag_config("buzz", foreground="#4fa8ff")
        self.txt_log.tag_config("ignored", foreground="#ffa500")
        self.txt_log.tag_config("alerte", foreground="#ff4040", font=("Monospace", 9, "bold"))
        self.txt_log.tag_config("normal", foreground="#dddddd")

        bottom = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        bottom.pack(fill="x")
        self.lbl_fichier = ttk.Label(bottom, text="", foreground="#888888")
        self.lbl_fichier.pack(side="left")

        ttk.Button(bottom, text="Ouvrir le dossier",
                   command=self._ouvrir_dossier).pack(side="right", padx=4)
        ttk.Button(bottom, text="Générer résumé maintenant",
                   command=self._bouton_resume).pack(side="right", padx=4)

    # ================= Fichiers =================
    def _ouvrir_fichier_log(self):
        nom = f"quiz_log_{self.session_start.strftime('%Y-%m-%d_%H-%M-%S')}.txt"
        # buffering=1 : écrit chaque ligne immédiatement sur le disque, pour ne
        # rien perdre en cas de fermeture brutale du programme.
        self.log_file = open(nom, "a", buffering=1, encoding="utf-8")
        self.nom_fichier = nom
        self.lbl_fichier.config(text=f"Log : {nom}")

    def _log_ecrire(self, texte):
        if self.log_file:
            self.log_file.write(texte + "\n")

    def _ouvrir_dossier(self):
        dossier = os.path.abspath(os.path.dirname(self.nom_fichier) or ".")
        try:
            systeme = platform.system()
            if systeme == "Windows":
                os.startfile(dossier)  # type: ignore[attr-defined]
            elif systeme == "Darwin":
                subprocess.Popen(["open", dossier])
            else:
                subprocess.Popen(["xdg-open", dossier])
        except Exception as e:
            self._ajouter_ligne(f"Impossible d'ouvrir le dossier : {e}", "alerte")

    def _generer_resume(self):
        fin = datetime.now()
        duree = str(fin - self.session_start).split(".")[0]

        lignes = [
            "=== RÉSUMÉ SESSION QUIZ LOGGER ===",
            f"Début : {self.session_start.strftime('%Y-%m-%d %H:%M:%S')}",
            f"Fin   : {fin.strftime('%Y-%m-%d %H:%M:%S')}",
            f"Durée : {duree}",
            f"Port  : {self.port_utilise or '(non connecté)'}",
            "",
            f"Lignes reçues au total : {self.stat_total_lignes}",
            f"Buzz détectés : {self.stat_nb_buzz}",
            f"Buzz ignorés (verrouillé/déjà joué) : {self.stat_nb_ignored}",
            f"Passages radio OK -> down (radioOK=0) : {self.stat_nb_radio_down}",
            "",
            f"--- FREEZES / SILENCES DÉTECTÉS ({len(self.silences)}) ---",
        ]
        if self.silences:
            lignes += [f"{h} - silence de {d:.1f}s" for h, d in self.silences]
        else:
            lignes.append("(aucun)")

        lignes.append("")
        lignes.append(f"--- REDÉMARRAGES MEGA DÉTECTÉS ({len(self.reboots)}) ---")
        if self.reboots:
            lignes += [f"{h} - reset watchdog (millis {a} -> {b})" for h, a, b in self.reboots]
        else:
            lignes.append("(aucun)")

        lignes.append("")
        lignes.append(f"Fichier log complet (détail ligne par ligne) : {self.nom_fichier}")

        texte = "\n".join(lignes)
        chemin = f"quiz_resume_{self.session_start.strftime('%Y-%m-%d_%H-%M-%S')}.txt"
        with open(chemin, "w", encoding="utf-8") as f:
            f.write(texte)
        return chemin, texte

    def _bouton_resume(self):
        chemin, _ = self._generer_resume()
        self._ajouter_ligne(f"--- Résumé généré : {chemin} ---", "normal")
        messagebox.showinfo(
            "Résumé généré",
            f"Fichier créé :\n{os.path.abspath(chemin)}\n\n"
            "C'est ce petit fichier qu'il faut envoyer pour le diagnostic "
            "(pas besoin d'envoyer le gros fichier log)."
        )

    # ================= Ports =================
    def _scanner_ports(self, auto_connect=False):
        ports = list(list_ports.comports())
        self._ports_disponibles = ports
        noms = [f"{p.device} - {p.description}" for p in ports]
        self.combo_ports["values"] = noms
        if noms:
            self.combo_ports.current(0)
        if auto_connect:
            if len(ports) == 1:
                # Un seul port détecté -> connexion automatique, pas de choix nécessaire.
                self._connecter(ports[0].device)
            elif not ports:
                self._ajouter_ligne("Aucun port série détecté pour le moment.", "normal")

    # ================= Connexion =================
    def _toggle_connexion(self):
        if self.ser and self.ser.is_open:
            self._deconnecter()
        else:
            idx = self.combo_ports.current()
            if idx < 0 or idx >= len(self._ports_disponibles):
                self._ajouter_ligne("Aucun port sélectionné.", "alerte")
                return
            self._connecter(self._ports_disponibles[idx].device)

    def _connecter(self, port):
        try:
            self.ser = serial.Serial(port, BAUDRATE, timeout=1)
        except serial.SerialException as e:
            self._ajouter_ligne(f"Impossible d'ouvrir {port} : {e}", "alerte")
            return
        self.reading = True
        self.port_utilise = port
        self.derniere_ligne_ts = time.monotonic()
        self._alerte_active = False
        threading.Thread(target=self._boucle_lecture, daemon=True).start()
        self.lbl_status.config(text=f"Connecté ({port})", foreground="#00a000")
        self.btn_connect.config(text="Déconnecter")
        self._ajouter_ligne(f"--- Connecté à {port} ---", "normal")

    def _deconnecter(self):
        self.reading = False
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.lbl_status.config(text="Déconnecté", foreground="#b00000")
        self.btn_connect.config(text="Connecter")
        self._ajouter_ligne("--- Déconnecté ---", "normal")

    # ================= Thread de lecture (arrière-plan) =================
    def _boucle_lecture(self):
        while self.reading and self.ser and self.ser.is_open:
            try:
                raw = self.ser.readline()
            except Exception as e:
                self.line_queue.put(("ERREUR", f"Erreur lecture port : {e}"))
                break
            if raw:
                ligne = raw.decode("utf-8", errors="ignore").strip()
                if ligne:
                    self.line_queue.put(("LIGNE", ligne))

    # ================= Traitement (thread principal Tk uniquement) =================
    def _poll_queue(self):
        while True:
            try:
                kind, payload = self.line_queue.get_nowait()
            except queue.Empty:
                break
            if kind == "LIGNE":
                self._traiter_ligne(payload)
            elif kind == "ERREUR":
                self._ajouter_ligne(payload, "alerte")
                self._deconnecter()
        self.root.after(POLL_MS, self._poll_queue)

    def _traiter_ligne(self, ligne):
        self.derniere_ligne_ts = time.monotonic()
        self.stat_total_lignes += 1
        horodatage = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        entree = f"{horodatage} | {ligne}"
        self._log_ecrire(entree)

        tag = "normal"
        if ligne.startswith("ALIVE:"):
            tag = "alive"
            m = ALIVE_RE.match(ligne)
            if m:
                millis_mega = int(m.group(1))
                radio_ok = m.group(2) == "1"

                if self.dernier_millis_mega is not None and millis_mega < self.dernier_millis_mega:
                    alerte = (f"{horodatage} | *** REDÉMARRAGE MEGA DÉTECTÉ "
                              f"(millis {self.dernier_millis_mega} -> {millis_mega}) ***")
                    self._ajouter_ligne(alerte, "alerte")
                    self._log_ecrire(alerte)
                    self.reboots.append((horodatage, self.dernier_millis_mega, millis_mega))
                self.dernier_millis_mega = millis_mega

                if self.dernier_radio_ok is True and radio_ok is False:
                    self.stat_nb_radio_down += 1
                self.dernier_radio_ok = radio_ok
        elif ligne.startswith("IGNORED:"):
            tag = "ignored"
            self.stat_nb_ignored += 1
        elif ligne.startswith("BUZZ:") or ligne.startswith("BUZZ_EQUIPE:"):
            tag = "buzz"
            self.stat_nb_buzz += 1
        elif ligne.startswith("WARN:") or ligne.startswith("ERR:"):
            tag = "alerte"

        self._ajouter_ligne(entree, tag)
        self._refresh_stats_label()

    def _ajouter_ligne(self, texte, tag="normal"):
        self.txt_log.config(state="normal")
        self.txt_log.insert("end", texte + "\n", tag)
        self.txt_log.see("end")
        self.txt_log.config(state="disabled")

    def _refresh_stats_label(self):
        self.lbl_stats.config(
            text=(f"Buzz: {self.stat_nb_buzz}   |   Ignorés: {self.stat_nb_ignored}   |   "
                  f"Silences: {len(self.silences)}   |   Redémarrages: {len(self.reboots)}")
        )

    # ================= Bandeau d'état =================
    def _refresh_banner(self):
        if self.ser and self.ser.is_open:
            silence = time.monotonic() - self.derniere_ligne_ts
            if silence > GAP_ALERT_SECONDS:
                self.lbl_banner.config(
                    text=f"AUCUNE DONNÉE DEPUIS {silence:.0f}s — FREEZE POSSIBLE",
                    bg="#b00000",
                )
                if not self._alerte_active:
                    self._alerte_active = True
                    horodatage = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    msg = f"{horodatage} | [SILENCE {silence:.1f}s - freeze Mega possible]"
                    self._ajouter_ligne(msg, "alerte")
                    self._log_ecrire(msg)
                    self.silences.append((horodatage, silence))
                    self._refresh_stats_label()
            else:
                self._alerte_active = False
                self.lbl_banner.config(text="EN DIRECT", bg="#007a00")
        else:
            self.lbl_banner.config(text="EN ATTENTE DE CONNEXION", bg="#444444")
        self.root.after(BANNER_REFRESH_MS, self._refresh_banner)

    # ================= Fermeture =================
    def _on_close(self):
        self.reading = False
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        try:
            self._generer_resume()
        except Exception:
            pass
        if self.log_file:
            self.log_file.close()
        self.root.destroy()


def main():
    root = tk.Tk()
    QuizLoggerApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
