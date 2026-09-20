import copy
import dearpygui.dearpygui as dpg
import json
import os
import queue
import serial
import serial.tools.list_ports
import threading
import time
from datetime import datetime
import re

# --- CONFIGURATION & FICHIERS ---
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(BASE_DIR, "config_quiz_pro.json")
ser = None

# Mises à jour UI depuis le thread de synchro (Dear PyGui : éviter dpg.* hors fil principal)
_ui_queue = queue.Queue()


def schedule_ui(fn):
    _ui_queue.put(fn)


def schedule_progress_bar(step: int, total: int):
    def fn():
        if total <= 0 or not dpg.does_item_exist("progress_bar"):
            return
        dpg.set_value("progress_bar", step / total)

    schedule_ui(fn)


def drain_ui_queue():
    while True:
        try:
            fn = _ui_queue.get_nowait()
        except queue.Empty:
            break
        try:
            fn()
        except Exception:
            pass

# VID USB connus des adaptateurs Arduino/USB-série (même liste que interface_30eq.py)
KNOWN_ARDUINO_VIDS = [0x2341, 0x2A03, 0x1A86, 0x10C4, 0x0403]

def get_clean_ports():
    """Scanne les ports USB sur Windows, Mac et Linux, en filtrant les faux
    positifs Bluetooth (cu.Bluetooth-Incoming-Port, cu.MALS, description
    contenant BLUETOOTH/INCOMING-PORT) qui perturbent la detection sous
    Mac/Linux, et en priorisant les VID Arduino connus."""
    try:
        all_ports = serial.tools.list_ports.comports()
    except Exception as e:
        # Ne jamais bloquer l'interface si la detection echoue
        return [f"ERREUR SCAN PORTS : {e}"]

    clean_list = []
    ftdi_sans_driver_suspect = False
    aucune_carte_trouvee = True

    for p in all_ports:
        desc = (p.description or "").upper()
        device = p.device
        hwid = (p.hwid or "").upper()
        vid = p.vid

        # Détection USB par description
        is_usb = any(k in desc for k in ["USB", "ARDUINO", "CH340", "CP210", "FTDI", "UART", "SERIAL"])

        # Priorité aux VID Arduino/USB-série connus (plus fiable que la description)
        if vid in KNOWN_ARDUINO_VIDS:
            is_usb = True
        if any(f"VID_{v:04X}" in hwid for v in KNOWN_ARDUINO_VIDS):
            is_usb = True

        # Mac : /dev/cu.usbserial ou /dev/cu.usbmodem
        if device.startswith("/dev/cu.usbserial") or device.startswith("/dev/cu.usbmodem"):
            is_usb = True

        # Linux : /dev/ttyUSB ou /dev/ttyACM
        if device.startswith("/dev/ttyUSB") or device.startswith("/dev/ttyACM"):
            is_usb = True

        # Exclusions Bluetooth : chemins Mac connus ET description (couvre
        # aussi les ports virtuels Bluetooth qui n'utilisent pas ces chemins,
        # ex. certains rfcomm Linux ou nouveaux noms macOS)
        is_bluetooth = (
            device.startswith("/dev/cu.Bluetooth")
            or device.startswith("/dev/cu.MALS")
            or "BLUETOOTH" in desc
            or "INCOMING-PORT" in desc
        )
        if is_bluetooth:
            is_usb = False

        # Windows : COM1 est quasi toujours un port systeme, jamais l'Arduino
        if device.upper() == "COM1":
            is_usb = False

        if is_usb:
            clean_list.append(f"{device} (USB)")
            aucune_carte_trouvee = False
        elif is_bluetooth:
            clean_list.append(f"{device} (Bluetooth - ignore)")
        else:
            clean_list.append(f"{device} (Interne)")

        # Carte FTDI visible par description mais VID inattendu -> pilote
        # VCP probablement absent (frequent sur macOS recent)
        if "FTDI" in desc and vid != 0x0403:
            ftdi_sans_driver_suspect = True

    if not clean_list:
        return ["AUCUN PORT DÉTECTÉ"]

    # schedule_ui() : get_clean_ports() peut tourner hors du thread principal
    # (scan en arriere-plan, voir refresh_ports()) -> jamais de dpg.* direct ici.
    if ftdi_sans_driver_suspect:
        schedule_ui(lambda: log(
            "Carte FTDI detectee sans VID standard : installer le pilote FTDI VCP si la connexion echoue.",
            color=[255, 200, 0]))
    elif aucune_carte_trouvee:
        schedule_ui(lambda: log(
            "Aucune carte Arduino/USB-serie detectee : verifier le cable, ou installer le pilote "
            "(FTDI VCP / CH340 / CP210x selon l'adaptateur, frequent sur macOS recent).",
            color=[255, 200, 0]))

    return clean_list

def log(message, color=[255, 255, 255]):
    if dpg.does_item_exist("log_list"):
        timestamp = datetime.now().strftime("%H:%M:%S")
        dpg.add_text(f"[{timestamp}] {message}", parent="log_list", color=color)
        dpg.set_y_scroll("log_child", dpg.get_y_scroll_max("log_child") + 50)

_ports_scanning = False

def refresh_ports():
    """Scanne les ports en arriere-plan : serial.tools.list_ports.comports()
    peut se bloquer plusieurs secondes quand le Bluetooth est actif (ports
    virtuels cu.Bluetooth-Incoming-Port / rfcomm), ce qui gelait toute
    l'interface Dear PyGui pendant le scan (thread principal)."""
    global _ports_scanning
    if _ports_scanning:
        return
    _ports_scanning = True
    if dpg.does_item_exist("btn_refresh_ports"):
        dpg.configure_item("btn_refresh_ports", enabled=False)

    def worker():
        global _ports_scanning
        ports = get_clean_ports()

        def apply():
            global _ports_scanning
            _ports_scanning = False
            if dpg.does_item_exist("btn_refresh_ports"):
                dpg.configure_item("btn_refresh_ports", enabled=True)
            if dpg.does_item_exist("port_combo"):
                dpg.configure_item("port_combo", items=ports)

        schedule_ui(apply)

    threading.Thread(target=worker, daemon=True).start()

# Profil de canaux par defaut pour un nouveau projecteur -- chaque
# projecteur garde desormais son PROPRE reglage (voir migration ci-dessous),
# ce qui permet de melanger des modeles differents (projecteur RGB simple,
# lyre utilisee juste pour sa couleur, etc.) sans jamais toucher au code.
FIXTURE_PROFILE_DEFAULTS = {
    "nb_canaux": 8,
    "off_dim": 0,
    "off_r": 1,
    "off_g": 2,
    "off_b": 3,
    "off_strobe": -1,    # -1 = pas de canal strobe (depend du projecteur)
    "strobe_value": 200, # valeur qui declenche le strobe (depend du projecteur)
    "strobe_repos": 0,   # valeur du canal strobe HORS strobe : 0 normalement,
                         # mais certaines lyres combinent obturateur+strobe sur
                         # le meme canal -- mettre ~255 pour elles (sinon la
                         # lumiere reste noire une fois le strobe termine)
    "mode": 0,           # 0 = RGB continu (off_r/off_g/off_b independants)
                         # 1 = roue de couleurs (off_r = canal unique de la
                         # roue, off_g/off_b ignores) -- pour les lyres
}


def load_config():
    """Charge la configuration et assure la compatibilité des données."""
    default_structure = {
        "equipes": [{"couleurs": [[255, 255, 255]]}],
        "groupes_dmx": [{"id": 1, "adresse": 1, **FIXTURE_PROFILE_DEFAULTS}],
    }

    try:
        with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
            data = json.load(f)
    except FileNotFoundError:
        return copy.deepcopy(default_structure)
    except json.JSONDecodeError as e:
        print(f"Erreur config JSON: {e}")
        return copy.deepcopy(default_structure)
    except Exception as e:
        print(f"Erreur config: {e}")
        return copy.deepcopy(default_structure)

    if not isinstance(data, dict):
        return copy.deepcopy(default_structure)

    if "groupes_dmx" not in data or not isinstance(data["groupes_dmx"], list) or not data["groupes_dmx"]:
        data["groupes_dmx"] = copy.deepcopy(default_structure["groupes_dmx"])
    if "equipes" not in data or not isinstance(data["equipes"], list) or not data["equipes"]:
        data["equipes"] = copy.deepcopy(default_structure["equipes"])

    # Migration : une ancienne config avait un seul reglage "dmx_universal"
    # partage par tous les projecteurs -- on l'utilise comme valeur de
    # depart pour chaque projecteur plutot que le defaut generique, pour
    # ne rien perdre des reglages deja en place.
    ancien_universel = data.get("dmx_universal")
    valeurs_depart = FIXTURE_PROFILE_DEFAULTS
    if isinstance(ancien_universel, dict):
        valeurs_depart = {**FIXTURE_PROFILE_DEFAULTS, **{
            k: ancien_universel[k] for k in FIXTURE_PROFILE_DEFAULTS if k in ancien_universel
        }}
    data.pop("dmx_universal", None)

    for grp in data["groupes_dmx"]:
        if not isinstance(grp, dict):
            continue
        for k, v in valeurs_depart.items():
            grp.setdefault(k, v)

    for eq in data["equipes"]:
        if not isinstance(eq, dict):
            continue
        if "couleurs" not in eq or not isinstance(eq["couleurs"], list):
            eq["couleurs"] = [[255, 255, 255] for _ in data["groupes_dmx"]]
        while len(eq["couleurs"]) < len(data["groupes_dmx"]):
            eq["couleurs"].append([255, 255, 255])
        # Duree du strobe a l'annonce du gagnant, PAR EQUIPE : 0 = aucun,
        # -1 = continu, >0 = duree en ms. Absent par defaut (pas de strobe)
        # tant que l'operateur ne l'active pas explicitement pour l'equipe.
        eq.setdefault("strobe_duree_ms", 0)

    return data

config = load_config()

def save_config():
    with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
        json.dump(config, f, indent=2)

# --- LOGIQUE DMX ---
def auto_adressage():
    """Recalcule les adresses en chainant chaque projecteur selon SON
    propre nombre de canaux -- fonctionne meme avec des projecteurs de
    modeles differents (ex: un simple RGB a 8 canaux suivi d'une lyre a
    16 canaux avancera correctement de 16, pas d'un pas unique partage)."""
    if not config["groupes_dmx"]: return
    addr = config["groupes_dmx"][0]["adresse"]
    for grp in config["groupes_dmx"]:
        grp["adresse"] = addr
        addr += grp.get("nb_canaux", FIXTURE_PROFILE_DEFAULTS["nb_canaux"])
    save_config()
    refresh_ui_full()
    log("Adressage auto genere (chaine selon les canaux de chaque projecteur).", color=[0, 255, 255])

# ========== FONCTIONS AJOUT/SUPPRESSION PROJECTEURS ==========
def add_projector():
    """Ajoute un nouveau projecteur. MAX 30. Reprend le profil de canaux
    du dernier projecteur existant comme point de depart (souvent le
    meme modele que celui d'a cote), plutot que le defaut generique."""
    if len(config["groupes_dmx"]) >= 30:
        log("Maximum 30 projecteurs !", color=[255, 50, 50])
        return
    new_id = len(config["groupes_dmx"]) + 1
    profil_depart = FIXTURE_PROFILE_DEFAULTS
    if config["groupes_dmx"]:
        dernier = config["groupes_dmx"][-1]
        profil_depart = {k: dernier.get(k, v) for k, v in FIXTURE_PROFILE_DEFAULTS.items()}
    config["groupes_dmx"].append({"id": new_id, "adresse": 1, **profil_depart})

    for eq in config["equipes"]:
        eq["couleurs"].append([255, 255, 255])

    save_config()
    refresh_ui_full()
    log(f"Projecteur {new_id} ajoute", color=[0, 255, 0])

def remove_projector():
    """Supprime le dernier projecteur."""
    if len(config["groupes_dmx"]) > 1:
        removed = config["groupes_dmx"].pop()
        
        for eq in config["equipes"]:
            if eq["couleurs"]:
                eq["couleurs"].pop()
        
        save_config()
        refresh_ui_full()
        log(f"Projecteur {removed['id']} supprime", color=[255, 150, 0])
    else:
        log("Impossible de supprimer le dernier projecteur", color=[255, 100, 0])

def reset_total_config():
    config["groupes_dmx"] = [{"id": 1, "adresse": 1, **FIXTURE_PROFILE_DEFAULTS}]
    config["equipes"] = [{"couleurs": [[255, 255, 255]]}]
    save_config()
    refresh_ui_full()
    log("Configuration reinitialisee par defaut.", color=[255, 100, 100])

def verifier_et_sauver():
    save_config()
    log("Configuration sauvegardee sur le PC.", color=[0, 255, 0])

# --- COMMUNICATIONS ---
def envoyer_configuration_complete():
    global ser
    if not ser or not ser.is_open:
        log("ERREUR : Arduino non connecte !", color=[255, 50, 50])
        return

    for i, grp in enumerate(config["groupes_dmx"]):
        adr = grp["adresse"]
        nb_canaux = grp.get("nb_canaux", FIXTURE_PROFILE_DEFAULTS["nb_canaux"])
        if adr < 1 or adr > 512:
            log(f"Erreur Proj {i+1} : Adresse {adr} invalide", color=[255, 0, 0])
            return
        if (adr + nb_canaux - 1) > 512:
            log(f"Erreur Proj {i+1} : Fin canal {adr + nb_canaux - 1} > 512", color=[255, 0, 0])
            return

    def thread_sync():
        try:
            def ui_start():
                if dpg.does_item_exist("progress_group"):
                    dpg.show_item("progress_group")
                if dpg.does_item_exist("progress_bar"):
                    dpg.set_value("progress_bar", 0.0)
                log("Synchronisation MEGA...", color=[255, 165, 0])

            schedule_ui(ui_start)

            ser.reset_input_buffer()
            time.sleep(0.2)

            # +1 palier de progression par projecteur, un SET_PATCH est
            # desormais envoye individuellement pour chacun (profil propre).
            # +1 palier par equipe pour SET_STROBE_EQ, en plus des patchs
            # par projecteur, des adresses, et des couleurs par equipe/projecteur.
            total_steps = (1 + len(config["groupes_dmx"]) + len(config["groupes_dmx"])
                           + (len(config["equipes"]) * len(config["groupes_dmx"]))
                           + len(config["equipes"]))
            current_step = 0

            for i, grp in enumerate(config["groupes_dmx"]):
                trame_patch = (f"SET_PATCH:{i}:{grp['nb_canaux']}:{grp['off_dim']}:{grp['off_r']}:{grp['off_g']}:"
                               f"{grp['off_b']}:{grp['off_strobe']}:{grp['strobe_value']}:"
                               f"{grp.get('strobe_repos', FIXTURE_PROFILE_DEFAULTS['strobe_repos'])}:"
                               f"{grp.get('mode', FIXTURE_PROFILE_DEFAULTS['mode'])}\n")
                ser.write(trame_patch.encode())
                current_step += 1
                schedule_progress_bar(current_step, total_steps)
                time.sleep(0.08)

            ser.write(f"SET_NB_EQ:{len(config['equipes'])}\n".encode())
            current_step += 1
            schedule_progress_bar(current_step, total_steps)
            time.sleep(0.1)

            for i, grp in enumerate(config["groupes_dmx"]):
                ser.write(f"SET_ADR:{i}:{grp['adresse']}\n".encode())
                current_step += 1
                schedule_progress_bar(current_step, total_steps)
                time.sleep(0.08)

            for e_idx, eq in enumerate(config["equipes"]):
                for g_idx, col in enumerate(eq["couleurs"]):
                    r, g, b = [max(0, min(255, int(c))) for c in col]
                    trame = f"SET_COL:{e_idx+1}:{g_idx}:{r}:{g}:{b}\n"
                    ser.write(trame.encode())
                    current_step += 1
                    schedule_progress_bar(current_step, total_steps)
                    time.sleep(0.05)

                duree = eq.get("strobe_duree_ms", 0)
                ser.write(f"SET_STROBE_EQ:{e_idx+1}:{duree}\n".encode())
                current_step += 1
                schedule_progress_bar(current_step, total_steps)
                time.sleep(0.05)

            ser.write(b"SAVE_CONFIG\n")
            schedule_ui(lambda: log("MEGA SYNCHRONISE !", color=[0, 255, 127]))
            time.sleep(0.5)
            
        except Exception as e:
            schedule_ui(lambda err=str(e): log(f"Erreur synchro : {err}", color=[255, 0, 0]))
        finally:
            def ui_done():
                if dpg.does_item_exist("progress_group"):
                    dpg.hide_item("progress_group")

            schedule_ui(ui_done)

    threading.Thread(target=thread_sync, daemon=True).start()

def parse_dump_mega(lignes):
    """Convertit les lignes CFG:... renvoyees par la Mega (GET_CONFIG) en
    (groupes_dmx, equipes) au format de config_quiz_pro.json. Leve ValueError
    si le dump est incomplet. Ce que la Mega ne stocke pas (rien d'autre que
    projecteurs, couleurs et strobes dans ce fichier) n'est pas concerne."""
    nb_eq = 0
    projs = {}
    strobes = {}
    couleurs = {}
    for ligne in lignes:
        p = ligne.strip().split(":")
        if len(p) < 3 or p[0] != "CFG":
            continue
        try:
            n = [int(v) for v in p[2:]]
        except ValueError:
            continue
        if p[1] == "NB_EQ":
            nb_eq = n[0]
        elif p[1] == "PROJ" and len(n) >= 11:
            projs[n[0]] = {
                "id": n[0] + 1, "adresse": n[1], "nb_canaux": n[2],
                "off_dim": n[3], "off_r": n[4], "off_g": n[5], "off_b": n[6],
                "off_strobe": n[7], "strobe_value": n[8], "strobe_repos": n[9],
                "mode": n[10],
            }
        elif p[1] == "STROBE" and len(n) >= 2:
            strobes[n[0] - 1] = n[1]
        elif p[1] == "COL" and len(n) >= 5:
            couleurs.setdefault(n[0] - 1, {})[n[1]] = [n[2], n[3], n[4]]

    if nb_eq < 1 or not projs:
        raise ValueError("configuration recue incomplete ou vide")

    groupes = [projs[i] for i in sorted(projs)]
    for i, grp in enumerate(groupes):
        grp["id"] = i + 1
    equipes = []
    for e in range(nb_eq):
        equipes.append({
            "couleurs": [couleurs.get(e, {}).get(g, [255, 255, 255]) for g in range(len(groupes))],
            "strobe_duree_ms": strobes.get(e, 0),
        })
    return groupes, equipes


def lire_configuration_mega():
    global ser
    if not ser or not ser.is_open:
        log("ERREUR : Arduino non connecte !", color=[255, 50, 50])
        return
    port = ser

    def thread_lecture():
        try:
            schedule_ui(lambda: log("Lecture de la configuration depuis la Mega...", color=[255, 165, 0]))
            port.reset_input_buffer()
            port.write(b"GET_CONFIG\n")
            lignes = []
            fin = time.time() + 15
            occupe = False
            termine = False
            while time.time() < fin:
                brut = port.readline().decode(errors="ignore").strip()
                if not brut:
                    continue
                if "GET_CONFIG_BUSY" in brut:
                    occupe = True
                    break
                if brut.startswith("CFG:"):
                    lignes.append(brut)
                    if brut == "CFG:END":
                        termine = True
                        break
            if occupe:
                schedule_ui(lambda: log("Mega en manche (buzz en cours) : valider/refuser puis relire.", color=[255, 150, 0]))
                return
            if not termine:
                schedule_ui(lambda: log("Aucune reponse complete de la Mega (firmware sans GET_CONFIG ?).", color=[255, 50, 50]))
                return
            groupes, equipes = parse_dump_mega(lignes)

            def appliquer():
                if os.path.exists(CONFIG_FILE):
                    try:
                        with open(CONFIG_FILE, 'r', encoding='utf-8') as f_in, \
                             open(CONFIG_FILE + ".bak", 'w', encoding='utf-8') as f_out:
                            f_out.write(f_in.read())
                    except OSError:
                        pass
                config["groupes_dmx"] = groupes
                config["equipes"] = equipes
                save_config()
                refresh_ui_full()
                log(f"Configuration lue depuis la Mega : {len(groupes)} projecteur(s), {len(equipes)} equipe(s). "
                    f"(ancienne config PC sauvegardee en .bak)", color=[0, 255, 127])

            schedule_ui(appliquer)
        except Exception as e:
            schedule_ui(lambda err=str(e): log(f"Erreur lecture Mega : {err}", color=[255, 0, 0]))

    threading.Thread(target=thread_lecture, daemon=True).start()


def toggle_connection():
    global ser
    brut = dpg.get_value("port_combo")
    if not brut or "SELECTIONNEZ" in brut or "AUCUN" in brut:
        log("Selectionnez un port COM.", color=[255, 200, 0])
        return

    if ser and ser.is_open:
        ser.close()
        ser = None
        log("Deconnecte.")
        dpg.configure_item("btn_conn", label="CONNECTER")
        dpg.bind_item_theme("btn_conn", "bleu_theme")
    else:
        if "Interne" in brut:
            log("Port systeme bloque.", color=[255, 150, 0])
            return
        if "Bluetooth" in brut:
            log("Port Bluetooth ignore (ce n'est pas l'Arduino).", color=[255, 150, 0])
            return

        port_final = None

        # Windows : COM3, COM4...
        match_win = re.search(r"(COM\d+)", brut)
        if match_win:
            port_final = match_win.group(1)

        # Mac : /dev/cu.usbserial-XXXX ou /dev/cu.usbmodem-XXXX
        match_mac = re.search(r"(/dev/cu\.[^\s\)]+)", brut)
        if match_mac:
            port_final = match_mac.group(1)

        # Linux : /dev/ttyUSB0 ou /dev/ttyACM0
        match_linux = re.search(r"(/dev/tty[^\s\)]+)", brut)
        if match_linux:
            port_final = match_linux.group(1)

        if not port_final:
            port_final = brut.split()[0]
        try:
            log(f"Connexion a {port_final}...")
            ser = serial.Serial(port=port_final, baudrate=9600, timeout=2)
            ser.reset_input_buffer()
            time.sleep(2)
            # Connexion directe sans vérification IDENT
            log(f"CONNECTE ! (Serial3 Mega via TTL, 9600)", color=[0, 255, 127])
            dpg.configure_item("btn_conn", label="DECONNECTER")
            dpg.bind_item_theme("btn_conn", "vert_theme")
        except Exception as e:
            ser = None
            log(f"Echec : {e}", color=[255, 50, 50])

# --- UI COMPONENTS ---
def build_equipe_section():
    with dpg.collapsing_header(label="GESTION DES EQUIPES", default_open=True):
        for i, eq in enumerate(config["equipes"]):
            with dpg.tree_node(label=f"EQUIPE {i+1}", default_open=True):
                for g_idx in range(len(config["groupes_dmx"])):
                    while len(eq["couleurs"]) < len(config["groupes_dmx"]):
                        eq["couleurs"].append([255, 255, 255])
                    
                    def color_callback(sender, app_data, user_data):
                        proj_idx, eq_idx = user_data
                        raw_col = app_data[:3]
                        final = [int(c * 255) if c <= 1.01 else int(c) for c in raw_col]
                        config["equipes"][eq_idx]["couleurs"][proj_idx] = final
                        save_config()

                    with dpg.group(horizontal=True):
                        dpg.add_text(f"P{g_idx+1}:", color=[180, 180, 180])
                        dpg.add_color_edit(
                            default_value=list(eq["couleurs"][g_idx]) + [255],
                            no_inputs=True, width=160,
                            display_type=dpg.mvColorEdit_uint8,
                            input_mode=dpg.mvColorEdit_uint8,
                            callback=color_callback, user_data=(g_idx, i)
                        )

                def strobe_duree_callback(sender, app_data, user_data=i):
                    config["equipes"][user_data]["strobe_duree_ms"] = app_data
                    save_config()

                dpg.add_input_int(
                    label="Strobe (0=aucun, -1=continu, ms sinon)",
                    default_value=eq.get("strobe_duree_ms", 0),
                    min_value=-1, min_clamped=True, width=140,
                    callback=strobe_duree_callback,
                )

        with dpg.group(horizontal=True):
            dpg.add_button(label="+ EQUIPE", width=120, callback=lambda: [
                config["equipes"].append({"couleurs": [[255,255,255] for _ in config["groupes_dmx"]], "strobe_duree_ms": 0}) if len(config["equipes"]) < 30 else log("Maximum 30 equipes !", color=[255,50,50]),
                save_config(),
                refresh_ui_full()
            ])
            btn_del = dpg.add_button(label="- EQUIPE", width=120, callback=lambda: [
                config["equipes"].pop() if len(config["equipes"]) > 1 else None, 
                save_config(),
                refresh_ui_full()
            ])
            dpg.bind_item_theme(btn_del, "rouge_theme")

def setup_ui():
    with dpg.window(label="MASTER CONTROL - QUIZ DMX", tag="main_window"):
        dpg.bind_item_theme("main_window", "global_theme")
        
        # --- CONNEXION ---
        with dpg.group(horizontal=True):
            dpg.add_combo(items=get_clean_ports(), tag="port_combo", width=340, default_value="SELECTIONNEZ PORT USB")
            dpg.add_button(label="ACTUALISER", tag="btn_refresh_ports", width=100, callback=lambda: refresh_ports())
            status_label = "DECONNECTER" if (ser and ser.is_open) else "CONNECTER"
            dpg.add_button(label=status_label, tag="btn_conn", callback=toggle_connection, width=150)
            dpg.bind_item_theme("btn_conn", "vert_theme" if (ser and ser.is_open) else "bleu_theme")

        dpg.add_text(
            "Mega : liaison PC sur Serial3 (USB-TTL 14/15), 9600 — choisir le COM du dongle TTL.",
            color=[140, 180, 210],
            wrap=1000,
        )

        dpg.add_spacer(height=10)
        dpg.add_separator()
        dpg.add_spacer(height=10)

        with dpg.group(horizontal=True):
            # COLONNE GAUCHE
            with dpg.child_window(width=460, border=True):
                dpg.add_text("CONFIGURATION SYSTEME", color=[0, 180, 255])
                
                # --- PROJECTEURS ---
                # Chaque projecteur a desormais son PROPRE profil de canaux
                # (nombre de canaux, offsets couleur/strobe) au lieu d'un
                # reglage unique impose a tous -- permet de melanger des
                # modeles differents (ex: un simple RGB a cote d'une lyre
                # utilisee juste pour sa couleur) sans toucher au code.
                with dpg.collapsing_header(label="PROJECTEURS (adresse + profil de canaux)", default_open=True):
                    dpg.add_button(label="CALCULER ADRESSES AUTO (chaine selon les canaux de chacun)",
                                   callback=auto_adressage, width=-1)
                    dpg.add_spacer(height=6)

                    for i, grp in enumerate(config["groupes_dmx"]):
                        with dpg.collapsing_header(label=f"Projecteur {i+1}", default_open=False):
                            dpg.add_input_int(
                                label="Adresse DMX", default_value=grp["adresse"],
                                width=110, min_value=1, max_value=512,
                                min_clamped=True, max_clamped=True,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"adresse": a}),
                            )
                            dpg.add_input_int(
                                label="Nombre de canaux", default_value=grp.get("nb_canaux", FIXTURE_PROFILE_DEFAULTS["nb_canaux"]),
                                width=110,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"nb_canaux": a}),
                            )
                            dpg.add_combo(
                                label="Mode couleur", items=["RGB continu", "Roue de couleurs (lyre)"],
                                default_value="Roue de couleurs (lyre)" if grp.get("mode", FIXTURE_PROFILE_DEFAULTS["mode"]) == 1 else "RGB continu",
                                width=200,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"mode": 1 if a.startswith("Roue") else 0}),
                            )
                            dpg.add_text("Offsets des canaux (0 = premier canal du projecteur) :", color=[150, 150, 150])
                            dpg.add_input_int(
                                label="Offset DIMMER", default_value=grp.get("off_dim", FIXTURE_PROFILE_DEFAULTS["off_dim"]),
                                width=110,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"off_dim": a}),
                            )
                            dpg.add_input_int(
                                label="Offset ROUGE (= canal roue si mode Roue de couleurs)", default_value=grp.get("off_r", FIXTURE_PROFILE_DEFAULTS["off_r"]),
                                width=110,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"off_r": a}),
                            )
                            dpg.add_input_int(
                                label="Offset VERT", default_value=grp.get("off_g", FIXTURE_PROFILE_DEFAULTS["off_g"]),
                                width=110,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"off_g": a}),
                            )
                            dpg.add_input_int(
                                label="Offset BLEU", default_value=grp.get("off_b", FIXTURE_PROFILE_DEFAULTS["off_b"]),
                                width=110,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"off_b": a}),
                            )
                            dpg.add_input_int(
                                label="Offset STROBE (-1 = aucun)", default_value=grp.get("off_strobe", FIXTURE_PROFILE_DEFAULTS["off_strobe"]),
                                width=110,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"off_strobe": a}),
                            )
                            dpg.add_input_int(
                                label="Valeur STROBE (0-255)", default_value=grp.get("strobe_value", FIXTURE_PROFILE_DEFAULTS["strobe_value"]),
                                width=110,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"strobe_value": a}),
                            )
                            dpg.add_input_int(
                                label="Valeur repos obturateur (0 normalement, ~255 si obturateur bloquant type lyre)",
                                default_value=grp.get("strobe_repos", FIXTURE_PROFILE_DEFAULTS["strobe_repos"]),
                                width=110,
                                callback=lambda s, a, u=i: config["groupes_dmx"][u].update({"strobe_repos": a}),
                            )
                            dpg.add_text("Strobe ~400ms a l'annonce du gagnant, puis couleur fixe.",
                                         color=[150, 150, 150], wrap=420)

                    dpg.add_spacer(height=5)
                    with dpg.group(horizontal=True):
                        dpg.add_button(
                            label="AJOUTER PROJECTEUR", 
                            callback=add_projector,
                            width=200
                        )
                        dpg.add_spacer(width=10)
                        btn_del_proj = dpg.add_button(
                            label="SUPPRIMER PROJECTEUR", 
                            callback=remove_projector,
                            width=200
                        )
                        dpg.bind_item_theme(btn_del_proj, "rouge_theme")
                
                build_equipe_section()

            # COLONNE DROITE
            with dpg.group():
                with dpg.child_window(height=265, border=True):
                    dpg.add_text("ACTIONS", color=[0, 180, 255])
                    dpg.add_button(label="SAUVER CONFIG PC", callback=verifier_et_sauver, width=-1, height=30)
                    dpg.add_spacer(height=5)
                    dpg.add_button(label="LIRE DEPUIS LA MEGA", callback=lire_configuration_mega, width=-1, height=30)
                    dpg.add_spacer(height=5)
                    btn_sync = dpg.add_button(label="SYNCHRONISER MEGA", callback=envoyer_configuration_complete, width=-1, height=45)
                    dpg.bind_item_theme(btn_sync, "bleu_theme")
                    with dpg.group(tag="progress_group", show=False):
                        dpg.add_progress_bar(tag="progress_bar", width=-1, height=12)
                    dpg.add_spacer(height=5)
                    btn_rst = dpg.add_button(label="RESET TOTAL CONFIG", callback=reset_total_config, width=-1)
                    dpg.bind_item_theme(btn_rst, "rouge_theme")

                dpg.add_text("CONSOLE LOG")
                with dpg.child_window(tag="log_child", border=True, height=-1):
                    dpg.add_group(tag="log_list")

def create_themes():
    with dpg.theme(tag="global_theme"):
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_style(dpg.mvStyleVar_WindowPadding, 15, 15)
            dpg.add_theme_style(dpg.mvStyleVar_FrameRounding, 6)
            dpg.add_theme_color(dpg.mvThemeCol_WindowBg, [25, 25, 30])
            dpg.add_theme_color(dpg.mvThemeCol_ChildBg, [32, 32, 45])
            dpg.add_theme_color(dpg.mvThemeCol_Header, [50, 50, 100])
    with dpg.theme(tag="vert_theme"):
        with dpg.theme_component(dpg.mvButton):
            dpg.add_theme_color(dpg.mvThemeCol_Button, [30, 140, 70])
    with dpg.theme(tag="rouge_theme"):
        with dpg.theme_component(dpg.mvButton):
            dpg.add_theme_color(dpg.mvThemeCol_Button, [150, 40, 40])
    with dpg.theme(tag="bleu_theme"):
        with dpg.theme_component(dpg.mvButton):
            dpg.add_theme_color(dpg.mvThemeCol_Button, [0, 110, 220])

def refresh_ui_full():
    if dpg.does_item_exist("main_window"): 
        dpg.delete_item("main_window")
    setup_ui()
    dpg.set_primary_window("main_window", True)

def main():
    dpg.create_context()
    create_themes()
    dpg.create_viewport(title="Quiz Config Mega PRO v2.0", width=1050, height=900)
    setup_ui()
    dpg.setup_dearpygui()
    dpg.show_viewport()
    dpg.set_primary_window("main_window", True)
    while dpg.is_dearpygui_running():
        drain_ui_queue()
        dpg.render_dearpygui_frame()
    dpg.destroy_context()


if __name__ == "__main__":
    main()