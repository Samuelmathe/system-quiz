"""
QUIZ BOARD V6 - Version finale
- Icône fenêtre / barre titre : dossier projet ``assets/`` (score.ico puis score.png)
- Console auto-nettoyante (garde les 10 dernières lignes)
- Fenêtre « CHRONO PROJECTEUR » : affichage grand format pour vidéoprojecteur (2e écran)
"""

import dearpygui.dearpygui as dpg
import serial
import serial.tools.list_ports
import json
import threading
import queue
import time
import os
import re
import sys
from typing import Optional, List, Dict, Any, Tuple
from datetime import datetime
from enum import Enum
from collections import deque

# Son via pygame
try:
    import pygame
    pygame.mixer.init()
    PYGAME_OK = True
except Exception:
    PYGAME_OK = False

def get_base_path():
    """Retourne le chemin du dossier où se trouve l'exe ou le script.
    Utilise sys.executable pour accéder aux fichiers EXTERNES (sounds/)
    placés à côté de l'exécutable par le client."""
    if getattr(sys, 'frozen', False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(__file__))


def resolve_sounds_dir(base_path: str) -> str:
    """Dossier contenant buzz.mp3, etc. Cherche ``sounds/`` à côté du script puis à la racine du projet."""
    candidates = [
        os.path.join(base_path, "sounds"),
        os.path.normpath(os.path.join(base_path, "..", "sounds")),
    ]
    for p in candidates:
        if os.path.isdir(p):
            return p
    return candidates[0]


def get_assets_dir():
    """Dossier des ressources image (icônes), à la racine du projet : ``assets/``.
    PyInstaller onefile : fichiers embarqués sous ``_MEIPASS/assets``."""
    if getattr(sys, 'frozen', False):
        meipass = getattr(sys, "_MEIPASS", None)
        if meipass:
            return os.path.join(meipass, "assets")
        return os.path.join(os.path.dirname(sys.executable), "assets")
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "assets"))


def apply_viewport_icons():
    """Barre de titre / dock : Dear PyGui — Windows attend surtout un .ico ; macOS .ico ou .png."""
    assets = get_assets_dir()
    # Préférer .ico (meilleure compatibilité Windows), sinon .png (souvent ok Linux / macOS)
    for name in ("score.ico", "score.png"):
        path = os.path.join(assets, name)
        if os.path.isfile(path):
            try:
                dpg.set_viewport_small_icon(path)
                dpg.set_viewport_large_icon(path)
                return True
            except Exception:
                continue
    return False


def find_bold_ui_font() -> Optional[str]:
    """Police système pour le texte géant du projecteur."""
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
        "C:/Windows/Fonts/Arialbd.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/Library/Fonts/Arial Bold.ttf",
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None

# =========================================================
# 1. CLASSES ET ENUMERATIONS
# =========================================================

class GameState(Enum):
    """Etats possibles du jeu."""
    IDLE = "idle"
    BUZZED = "buzzed"
    FINISHED = "finished"

# =========================================================
# 2. CLASSE PRINCIPALE
# =========================================================

class QuizController:
    def __init__(self):
        # Configuration
        self.base_path = get_base_path()
        self.config_file = os.path.join(self.base_path, "quiz_board_config.json")
        self.baud_rate = 9600
        self.serial_timeout = 1.0
        self.auto_save_interval = 5
        
        # Etat du jeu
        self.state = GameState.IDLE
        self.question_value = 1
        self.current_team: Optional[int] = None
        self.banned_teams: List[int] = []
        self.game_finished = False
        
        # Donnees
        self.teams: Dict[int, dict] = {}
        self.config: Dict[str, Any] = {}
        
        # Communication serie (lignes / evenements traites sur le thread GUI — Dear PyGui n'est pas thread-safe)
        self.serial_connection: Optional[serial.Serial] = None
        self.serial_running = False
        self.serial_thread: Optional[threading.Thread] = None
        self._serial_line_queue: queue.Queue = queue.Queue()
        self._serial_ctrl_queue: queue.Queue = queue.Queue()
        self._serial_connecting = False
        self._serial_log_rx = True  # afficher BUZZ/CMD et lignes inconnues dans la console
        # Un seul port serie PC : Mega TTL *ou* Nano son USB, jamais les deux.
        # "mega" = BUZZ: + CMD_SENT: + sons pygame.
        # "nano_son" = FWD_SON:200 (buzz) + CMD_SENT: (animateur via radio Mega) ; Mega sans USB PC.
        self.serial_device_kind: str = "mega"
        # Nano + AUDIO_PC : buzz via FWD_SON:200 ; valider/faux via CMD_SENT: depuis le nano.
        self.nano_audio_on_pc: bool = False
        
        # Interface
        self.theme_id: Optional[str] = None
        self.current_tab = "tab_scores"
        
        # Auto-save
        self.last_save_time = time.time()
        self.save_pending = False
        
        # Constantes
        self.MIN_TEAMS = 1
        self.MAX_TEAMS = 30
        self.CHRONO_MIN_SEC = 5
        self.CHRONO_MAX_SEC = 300
        self.CHRONO_BLINK_LAST_SEC = 3
        self.CHRONO_BLINK_INTERVAL = 0.25
        
        # ========== GESTION DE LA CONSOLE ==========
        self.log_lines = deque(maxlen=10)  # Garde seulement 10 lignes
        
        # ========== SONS ==========
        self.son_buzz     = None
        self.son_victoire = None
        self.son_echec    = None
        self.sons_equipes = {}  # {team_id: pygame.Sound}
        self.sounds_dir = resolve_sounds_dir(self.base_path)
        self._charger_sons()
        
        # Chronomètre (compte à rebours après buzz) + affichage projecteur
        self.chrono_enabled = True
        self.chrono_duration_sec = 30
        self.chrono_timeout_faux = True
        self.chrono_deadline: Optional[float] = None
        self._chrono_timeout_handled = False
        self._chrono_blink_on = False
        self._chrono_blink_last = 0.0
        self.font_proj_timer: Optional[int] = None
        self.font_proj_team: Optional[int] = None
        self.font_proj_ecoute: Optional[int] = None
        self.font_proj_points: Optional[int] = None
        self.font_proj_podium_title: Optional[int] = None
        self.font_proj_podium_name: Optional[int] = None
        self.font_proj_podium_score: Optional[int] = None
        self.public_display_open = False

        # Initialisation
        self.load_config()
        self._apply_chrono_config()
        self.initialize_teams()
    
    # =====================================================
    # 2.1 GESTION DE CONFIGURATION
    # =====================================================
    
    def _charger_sons(self):
        """Charge les fichiers MP3 depuis le dossier sounds/
        
        Structure attendue dans sounds/ :
        - buzz.mp3          → son buzz général (fallback)
        - victoire.mp3      → bonne réponse
        - echec.mp3         → mauvaise réponse
        - equipe_1.mp3      → son spécifique équipe 1 (optionnel)
        - equipe_2.mp3      → son spécifique équipe 2 (optionnel)
        - ...
        - equipe_30.mp3     → son spécifique équipe 30 (optionnel)
        """
        if not PYGAME_OK:
            return
        try:
            sounds_dir = self.sounds_dir
            
            buzz_path     = os.path.join(sounds_dir, "buzz.mp3")
            victoire_path = os.path.join(sounds_dir, "victoire.mp3")
            echec_path    = os.path.join(sounds_dir, "echec.mp3")

            if os.path.exists(buzz_path):
                self.son_buzz = pygame.mixer.Sound(buzz_path)
            if os.path.exists(victoire_path):
                self.son_victoire = pygame.mixer.Sound(victoire_path)
            if os.path.exists(echec_path):
                self.son_echec = pygame.mixer.Sound(echec_path)

            # Charger sons spécifiques par équipe (equipe_1.mp3 à equipe_30.mp3)
            self.sons_equipes = {}
            for i in range(1, 31):
                son_path = os.path.join(sounds_dir, f"equipe_{i}.mp3")
                if os.path.exists(son_path):
                    try:
                        self.sons_equipes[i-1] = pygame.mixer.Sound(son_path)
                    except Exception as e:
                        print(f"Erreur son equipe {i}: {e}")

        except Exception as e:
            print(f"Erreur chargement sons: {e}")

    def _son_buzz_equipe(self, team_id: int):
        """Son dédié buzz : ``sounds/equipe_{N}.mp3`` avec N = numéro équipe affiché (1..30). Chargement paresseux."""
        if not PYGAME_OK:
            return None
        if team_id in self.sons_equipes:
            return self.sons_equipes[team_id]
        n = team_id + 1
        if n < 1 or n > 30:
            return None
        path = os.path.join(self.sounds_dir, f"equipe_{n}.mp3")
        if not os.path.exists(path):
            return None
        try:
            snd = pygame.mixer.Sound(path)
            self.sons_equipes[team_id] = snd
            return snd
        except Exception:
            return None

    def _recharger_sons_equipes(self):
        """Recharge uniquement les sons équipes (utile si le client ajoute des fichiers)"""
        if not PYGAME_OK:
            return
        sounds_dir = self.sounds_dir
        self.sons_equipes = {}
        for i in range(1, 31):
            son_path = os.path.join(sounds_dir, f"equipe_{i}.mp3")
            if os.path.exists(son_path):
                try:
                    self.sons_equipes[i-1] = pygame.mixer.Sound(son_path)
                except Exception:
                    pass

    def _audio_use_external_df(self) -> bool:
        """True = envoyer les sons vers le DFPlayer (SON: / STOP). Faux si Mega ou Nano en mode haut-parleurs PC."""
        if not self.serial_connection or not self.serial_connection.is_open:
            return False
        if getattr(self, "serial_device_kind", "mega") != "nano_son":
            return False
        return not getattr(self, "nano_audio_on_pc", True)

    def _serial_write_line(self, data: bytes) -> None:
        if not self.serial_connection or not self.serial_connection.is_open:
            return
        try:
            self.serial_connection.write(data)
            self.serial_connection.flush()
        except Exception:
            pass

    def arreter_sons(self):
        """Coupe tous les sons (canaux) — utile quand buzz équipe passe à victoire/échec."""
        if self._audio_use_external_df():
            self._serial_write_line(b"STOP\n")
        if not PYGAME_OK:
            return
        try:
            pygame.mixer.stop()
        except Exception:
            pass
    
    def jouer_son(self, son):
        """Joue un son en arrière plan sans bloquer l'interface"""
        if self._audio_use_external_df():
            return
        if not PYGAME_OK or son is None:
            return
        try:
            threading.Thread(target=son.play, daemon=True).start()
        except Exception:
            pass
    
    def _play_fwd_son_line(self, line: str):
        """Buzz radio → Nano (AUDIO_PC) : FWD_SON:200 comme BUZZ:.
        Valider / faux : CMD_SENT depuis le Nano (201/202 radio) ; FWD_SON:201/202 conservé pour ancien firmware."""
        if getattr(self, "serial_device_kind", "mega") != "nano_son":
            return
        if not getattr(self, "nano_audio_on_pc", False):
            return
        try:
            parts = line.strip().split(":")
            if len(parts) >= 3 and parts[1] == "200" and parts[2].isdigit():
                team_1 = int(parts[2])
                tid = team_1 - 1
                if 0 <= tid < len(self.teams):
                    self.handle_buzz(tid)
                return
            if len(parts) >= 2 and parts[1] == "201":
                if self.state == GameState.BUZZED and self.current_team is not None:
                    self.handle_correct_answer(from_hardware=True)
                elif PYGAME_OK:
                    self.jouer_son(self.son_victoire)
                return
            if len(parts) >= 2 and parts[1] == "202":
                if self.state == GameState.BUZZED:
                    self.handle_wrong_answer(from_hardware=True)
                elif PYGAME_OK:
                    self.jouer_son(self.son_echec)
                return
        except (ValueError, IndexError, TypeError):
            pass

    def load_config(self):
        default_config = {
            "nb_equipes": 8,
            "auto_save": True,
            "last_scores": {},
            # Points ajoutés à l'équipe dès le buzz (0 = désactivé ; la bonne réponse ajoute encore question_value).
            "points_au_buzz": 0,
            # Chronomètre (affiché sur la fenêtre projecteur)
            "chrono_enabled": True,
            "chrono_duration_sec": 30,
            "chrono_timeout_faux": True,
            # Position/taille fenêtre projecteur (2e écran : souvent x=1920)
            "projector_x": 1920,
            "projector_y": 0,
            "projector_width": 1920,
            "projector_height": 1080,
        }
        
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, "r", encoding='utf-8') as f:
                    loaded = json.load(f)
                    default_config.update(loaded)
            except Exception as e:
                print(f"Erreur chargement config: {e}")
        
        self.config = default_config

    def _clamp_chrono_duration(self, value) -> int:
        try:
            sec = int(value)
        except (TypeError, ValueError):
            sec = 30
        return max(self.CHRONO_MIN_SEC, min(self.CHRONO_MAX_SEC, sec))

    def _apply_chrono_config(self):
        self.chrono_enabled = bool(self.config.get("chrono_enabled", True))
        self.chrono_duration_sec = self._clamp_chrono_duration(
            self.config.get("chrono_duration_sec", 30)
        )
        self.chrono_timeout_faux = bool(self.config.get("chrono_timeout_faux", True))

    def _persist_chrono_config(self):
        self.config["chrono_enabled"] = self.chrono_enabled
        self.config["chrono_duration_sec"] = self.chrono_duration_sec
        self.config["chrono_timeout_faux"] = self.chrono_timeout_faux
        self.save_pending = True
    
    def initialize_teams(self):
        nb_equipes = self.config.get("nb_equipes", 6)
        self.teams.clear()
        
        for i in range(nb_equipes):
            self.teams[i] = {
                "id": i,
                "name": f"Equipe {i+1}",
                "score": self.config.get("last_scores", {}).get(str(i), 0)
            }
    
    def save_config(self):
        try:
            scores = {str(team_id): team["score"] for team_id, team in self.teams.items()}
            self.config["last_scores"] = scores
            self.config["nb_equipes"] = len(self.teams)
            self.config["last_save"] = datetime.now().isoformat()
            
            with open(self.config_file, "w", encoding='utf-8') as f:
                json.dump(self.config, f, indent=2, ensure_ascii=False)
            
            return True
        except Exception as e:
            print(f"Erreur sauvegarde config: {e}")
            return False
    
    # =====================================================
    # 2.2 GESTION DES EQUIPES
    # =====================================================
    
    def update_team_score(self, team_id: int, delta: int):
        if team_id not in self.teams:
            return
        
        team = self.teams[team_id]
        old_score = team["score"]
        team["score"] = max(0, old_score + delta)
        
        self.update_team_display(team_id)
        
        if self.current_tab == "tab_podium":
            self.update_podium_display()
        
        self.save_pending = True
    
    def add_team(self):
        if len(self.teams) >= self.MAX_TEAMS:
            self.add_log(f"Limite de {self.MAX_TEAMS} equipes atteinte !", color=[255, 50, 50])
            return False
        
        new_id = max(self.teams.keys()) + 1 if self.teams else 0
        
        self.teams[new_id] = {
            "id": new_id,
            "name": f"Equipe {new_id+1}",
            "score": 0
        }
        
        self.add_log(f"Equipe {new_id+1} ajoutee", color=[0, 255, 0])
        self.refresh_teams_display()
        
        if self.current_tab == "tab_podium":
            self.update_podium_display()
        
        return True
    
    def remove_team(self):
        if len(self.teams) <= self.MIN_TEAMS:
            self.add_log(f"Impossible de descendre en dessous de {self.MIN_TEAMS} equipe(s)", 
                        color=[255, 50, 50])
            return False
        
        last_id = max(self.teams.keys())
        del self.teams[last_id]
        
        self.add_log(f"Equipe {last_id+1} supprimee", color=[255, 150, 0])
        self.refresh_teams_display()
        
        if self.current_tab == "tab_podium":
            self.update_podium_display()
        
        return True
    
    # =====================================================
    # 2.3 CALLBACKS BOUTONS
    # =====================================================
    
    def plus_score_callback(self, sender, app_data, user_data):
        team_id = user_data
        self.update_team_score(team_id, self.question_value)
        self.add_log(f"+{self.question_value} pts - Equipe {team_id+1}", color=[100, 255, 100])
    
    def minus_score_callback(self, sender, app_data, user_data):
        team_id = user_data
        self.update_team_score(team_id, -self.question_value)
        self.add_log(f"-{self.question_value} pts - Equipe {team_id+1}", color=[255, 100, 100])
    
    def correct_answer_callback(self, sender, app_data, user_data):
        team_id = user_data
        if self.state == GameState.BUZZED and self.current_team == team_id:
            self.handle_correct_answer(from_hardware=False)
    
    def wrong_answer_callback(self, sender, app_data, user_data):
        team_id = user_data
        if self.state == GameState.BUZZED and self.current_team == team_id:
            self.handle_wrong_answer(from_hardware=False)
    
    # =====================================================
    # 2.4 LOGIQUE DE JEU
    # =====================================================

    def _team_display_name(self, team_id: int) -> str:
        if team_id in self.teams:
            return self.teams[team_id].get("name") or f"Equipe {team_id + 1}"
        return f"Equipe {team_id + 1}"

    def _set_projector_view(self, mode: str) -> None:
        """mode: idle | buzz | podium"""
        if not dpg.does_item_exist("proj_view_buzz"):
            return
        dpg.configure_item("proj_view_buzz", show=(mode == "buzz"))
        if dpg.does_item_exist("proj_view_podium"):
            dpg.configure_item("proj_view_podium", show=(mode == "podium"))

    def _format_chrono(self, seconds: float) -> str:
        s = max(0, int(seconds + 0.999))
        return f"{s // 60:02d}:{s % 60:02d}"

    def chrono_remaining(self) -> Optional[float]:
        if self.chrono_deadline is None:
            return None
        return max(0.0, self.chrono_deadline - time.time())

    def chrono_start(self):
        if not self.chrono_enabled:
            self.chrono_deadline = None
            self._chrono_timeout_handled = False
            self.update_projector_display()
            return
        self.chrono_deadline = time.time() + self.chrono_duration_sec
        self._chrono_timeout_handled = False
        self._chrono_blink_on = False
        self._chrono_blink_last = time.time()
        self.update_projector_display()

    def chrono_stop(self):
        self.chrono_deadline = None
        self._chrono_timeout_handled = False
        self.update_projector_display()

    def tick_chrono(self):
        if self.chrono_deadline is None:
            return
        remaining = self.chrono_remaining()
        if remaining is None:
            return
        if 0 < remaining <= self.CHRONO_BLINK_LAST_SEC:
            now = time.time()
            if now - self._chrono_blink_last >= self.CHRONO_BLINK_INTERVAL:
                self._chrono_blink_on = not self._chrono_blink_on
                self._chrono_blink_last = now
        else:
            self._chrono_blink_on = False
        self.update_projector_display(remaining)
        if (
            remaining <= 0
            and self.state == GameState.BUZZED
            and self.chrono_timeout_faux
            and not self._chrono_timeout_handled
        ):
            self._chrono_timeout_handled = True
            self.add_log("Temps ecoule — mauvaise reponse", color=[255, 80, 80])
            self.handle_wrong_answer(from_hardware=False)

    def update_public_podium_display(self):
        if not dpg.does_item_exist("proj_podium_container"):
            return
        dpg.delete_item("proj_podium_container", children_only=True)
        sorted_teams = self.get_sorted_teams()
        if not sorted_teams:
            dpg.add_text("Aucune equipe", parent="proj_podium_container", color=[200, 200, 200])
            return

        dpg.add_spacer(height=30, parent="proj_podium_container")
        title = dpg.add_text(
            "CLASSEMENT FINAL",
            parent="proj_podium_container",
            color=[255, 215, 0],
        )
        if self.font_proj_podium_title:
            dpg.bind_item_font(title, self.font_proj_podium_title)
        dpg.add_spacer(height=40, parent="proj_podium_container")

        with dpg.group(horizontal=True, parent="proj_podium_container"):
            dpg.add_spacer(width=40)
            if len(sorted_teams) > 1:
                self._add_public_podium_slot(sorted_teams[1], "2e", [192, 192, 192], 280)
            dpg.add_spacer(width=30)
            self._add_public_podium_slot(sorted_teams[0], "1er", [255, 215, 0], 340)
            dpg.add_spacer(width=30)
            if len(sorted_teams) > 2:
                self._add_public_podium_slot(sorted_teams[2], "3e", [205, 127, 50], 280)

        if len(sorted_teams) > 3:
            dpg.add_spacer(height=50, parent="proj_podium_container")
            for i, team in enumerate(sorted_teams[3:], start=4):
                with dpg.group(horizontal=True, parent="proj_podium_container"):
                    dpg.add_spacer(width=120)
                    line = dpg.add_text(
                        f"{i}.  {self._team_display_name(team['id'])}  —  {team['score']} pts",
                        color=[180, 180, 200],
                    )
                    if self.font_proj_podium_score:
                        dpg.bind_item_font(line, self.font_proj_podium_score)

    def _add_public_podium_slot(self, team: dict, place: str, color, width: int):
        with dpg.group(width=width):
            t_place = dpg.add_text(place, color=color)
            name = self._team_display_name(team["id"])
            t_name = dpg.add_text(name, color=[240, 240, 245])
            t_score = dpg.add_text(f"{team['score']} pts", color=color)
            if self.font_proj_podium_name:
                dpg.bind_item_font(t_place, self.font_proj_podium_score or self.font_proj_podium_name)
                dpg.bind_item_font(t_name, self.font_proj_podium_name)
            if self.font_proj_podium_score:
                dpg.bind_item_font(t_score, self.font_proj_podium_score)

    def update_projector_display(self, remaining: Optional[float] = None):
        if not dpg.does_item_exist("txt_proj_timer"):
            return

        if self.state == GameState.FINISHED:
            self._set_projector_view("podium")
            self.update_public_podium_display()
            return

        if remaining is None:
            remaining = self.chrono_remaining()

        if self.state == GameState.BUZZED and self.current_team is not None:
            self._set_projector_view("buzz")
            tid = self.current_team
            team_name = self._team_display_name(tid)
            dpg.set_value("txt_proj_team", team_name)
            dpg.configure_item("txt_proj_team", show=True)
            dpg.set_value("txt_proj_ecoute", "EN ÉCOUTE")
            dpg.configure_item("txt_proj_ecoute", show=True)
            pts_label = f"{self.question_value} POINT{'S' if self.question_value != 1 else ''}"
            dpg.set_value("txt_proj_points", pts_label)
            dpg.configure_item("txt_proj_points", show=True)

            if self.chrono_enabled and remaining is not None:
                timer_text = self._format_chrono(remaining)
                if remaining <= 0:
                    timer_color = [255, 50, 50]
                elif remaining <= self.CHRONO_BLINK_LAST_SEC:
                    timer_color = [255, 40, 40] if self._chrono_blink_on else [255, 255, 255]
                else:
                    timer_color = [255, 255, 255]
            else:
                timer_text = "--:--"
                timer_color = [200, 200, 220]
            dpg.set_value("txt_proj_timer", timer_text)
            dpg.configure_item("txt_proj_timer", color=timer_color, show=True)
        else:
            self._set_projector_view("idle")

    def apply_public_viewport(self):
        """Place la fenêtre public sur le 2e écran (quiz_board_config.json)."""
        if not dpg.does_item_exist("projector_window"):
            return
        px = int(self.config.get("projector_x", 1920))
        py = int(self.config.get("projector_y", 0))
        pw = int(self.config.get("projector_width", 1920))
        ph = int(self.config.get("projector_height", 1080))
        dpg.configure_item("projector_window", pos=[px, py], width=pw, height=ph)

    def open_public_display(self, sender=None, app_data=None):
        """Ouvre l'écran public (chrono seul) sur le vidéoprojecteur — sans détails opérateur."""
        if not dpg.does_item_exist("projector_window"):
            return
        self.apply_public_viewport()
        dpg.configure_item("projector_window", show=True)
        self.public_display_open = True
        self.update_projector_display()
        self.add_log(
            "Affichage PUBLIC ouvert — glissez la fenetre noire sur le videoprojecteur "
            "(mode Etendu, pas Dupliquer). Scores/console restent sur l'ecran animateur.",
            color=[180, 220, 255],
        )

    def close_public_display(self):
        if not self.public_display_open:
            return
        if dpg.does_item_exist("projector_window"):
            dpg.configure_item("projector_window", show=False)
        self.public_display_open = False

    def toggle_public_display(self, sender=None, app_data=None):
        if self.public_display_open:
            self.close_public_display()
            self.add_log("Affichage public ferme", color=[180, 180, 200])
        else:
            self.open_public_display()

    def apply_projector_layout(self):
        """Compatibilité : repositionne la fenêtre public."""
        self.apply_public_viewport()

    def chrono_enabled_callback(self, sender, app_data):
        self.chrono_enabled = bool(app_data)
        self._persist_chrono_config()
        if not self.chrono_enabled:
            self.chrono_stop()
        elif self.state == GameState.BUZZED:
            self.chrono_start()
        else:
            self.update_projector_display()
        self.add_log(
            f"Chrono {'actif' if self.chrono_enabled else 'desactive'}",
            color=[160, 220, 255],
        )

    def set_chrono_duration(self, seconds, log: bool = True):
        clamped = self._clamp_chrono_duration(seconds)
        self.chrono_duration_sec = clamped
        if dpg.does_item_exist("input_chrono_duration"):
            dpg.set_value("input_chrono_duration", clamped)
        self._persist_chrono_config()
        if self.state != GameState.BUZZED:
            self.update_projector_display()
        if log:
            self.add_log(f"Duree chrono : {clamped} s", color=[160, 220, 255])

    def chrono_duration_input_callback(self, sender, app_data):
        self.set_chrono_duration(app_data, log=True)

    def chrono_timeout_callback(self, sender, app_data):
        self.chrono_timeout_faux = bool(app_data)
        self._persist_chrono_config()

    def open_projector_callback(self, sender=None, app_data=None):
        self.open_public_display(sender, app_data)
    
    def handle_buzz(self, team_id: int):
        if self.state == GameState.FINISHED:
            self.add_log(f"Jeu termine - buzz ignore", color=[255, 150, 0])
            return
        
        if team_id not in self.teams:
            return
        
        if self.state != GameState.IDLE:
            self.add_log(f"Equipe {team_id+1} buzz - pas en attente", color=[255, 150, 0])
            return
        
        if team_id in self.banned_teams:
            self.add_log(f"Equipe {team_id+1} bannie", color=[255, 100, 0])
            return
        
        self.state = GameState.BUZZED
        self.current_team = team_id
        self.banned_teams.append(team_id)

        buzz_pts = int(self.config.get("points_au_buzz", 0) or 0)
        if buzz_pts > 0:
            self.update_team_score(team_id, buzz_pts)
            self.add_log(
                f"Buzz comptabilise : +{buzz_pts} pt(s) — Equipe {team_id + 1}",
                color=[160, 255, 140],
            )
        
        # Son buzz : Nano DF (SON:200) ou PC — priorité ``equipe_N.mp3`` puis buzz.mp3
        if self._audio_use_external_df():
            self._serial_write_line(f"SON:200:{team_id + 1}\n".encode("ascii", errors="replace"))
        else:
            son_eq = self._son_buzz_equipe(team_id)
            if son_eq is not None:
                self.jouer_son(son_eq)
            else:
                self.jouer_son(self.son_buzz)
        
        self.update_game_display()
        self.show_validation_buttons(team_id, True)
        if not self.public_display_open:
            self.open_public_display()
        self.chrono_start()
        
        self.add_log(f"Equipe {team_id + 1} a buzze ! (id equipe {team_id + 1})", color=[255, 255, 0])

    def handle_correct_answer(self, from_hardware: bool = False):
        if self.state != GameState.BUZZED or self.current_team is None:
            return
        
        team_id = self.current_team
        self.update_team_score(team_id, self.question_value)
        self.add_log(f"Bonne reponse ! +{self.question_value} pts Equipe {team_id+1}", 
                    color=[0, 255, 0])
        
        # Arrêter le buzz / son équipe en cours puis jouer la victoire
        self.arreter_sons()
        if not self._audio_use_external_df():
            self.jouer_son(self.son_victoire)
        
        if self.serial_connection and self.serial_connection.is_open and not from_hardware:
            try:
                self.serial_connection.write(b"RESET_ALL\n")
            except Exception:
                pass
        
        self.chrono_stop()
        self.reset_question()
    
    def handle_wrong_answer(self, from_hardware: bool = False):
        if self.state != GameState.BUZZED:
            return
        
        tid = self.current_team
        self.add_log(f"Mauvaise reponse Equipe {tid + 1}", color=[255, 100, 0])
        
        # Arrêter le buzz / son équipe en cours puis jouer l'échec
        self.arreter_sons()
        if not self._audio_use_external_df():
            self.jouer_son(self.son_echec)
        
        if self.serial_connection and self.serial_connection.is_open and not from_hardware:
            try:
                self.serial_connection.write(b"RELANCE_PARTIEL\n")
            except Exception:
                pass
        
        self.state = GameState.IDLE
        self.current_team = None
        self.chrono_stop()
        self.hide_all_validation_buttons()
        self.update_game_display()
        self.update_projector_display()
    
    def reset_question(self):
        self.state = GameState.IDLE
        self.current_team = None
        self.banned_teams.clear()
        self.chrono_stop()
        self.hide_all_validation_buttons()
        self.update_game_display()
        self.update_projector_display()
    
    def reset_game(self):
        self.state = GameState.IDLE
        self.game_finished = False
        self.current_team = None
        self.banned_teams.clear()
        self.chrono_stop()
        self.hide_all_validation_buttons()
        
        for team_id in self.teams:
            self.teams[team_id]["score"] = 0
            self.update_team_display(team_id)
        
        if self.serial_connection and self.serial_connection.is_open:
            try:
                self.serial_connection.write(b"RESET_ALL\n")
            except Exception:
                pass
        
        self.switch_tab("tab_scores")
        self.update_projector_display()
        self.add_log("Jeu reinitialise", color=[200, 200, 0])
    
    def finish_game(self):
        self.state = GameState.FINISHED
        self.game_finished = True
        self.chrono_stop()
        if not self.public_display_open:
            self.open_public_display()
        self.update_projector_display()
        self.switch_tab("tab_podium")
        self.update_podium_display()
        self.add_log("Fin du jeu - Classement final", color=[255, 215, 0])
    
    def continue_game(self):
        self.state = GameState.IDLE
        self.game_finished = False
        self.current_team = None
        self.banned_teams.clear()
        self.chrono_stop()
        self.hide_all_validation_buttons()
        self.update_game_display()
        self.update_projector_display()
        self.switch_tab("tab_scores")
        self.add_log("Reprise du jeu", color=[0, 200, 255])
    
    # =====================================================
    # 2.5 GESTION DU PODIUM
    # =====================================================
    
    def get_sorted_teams(self) -> List[Dict]:
        return sorted(self.teams.values(), key=lambda x: x["score"], reverse=True)
    
    def create_podium_display(self):
        if dpg.does_item_exist("podium_container"):
            dpg.delete_item("podium_container", children_only=True)
        else:
            return
        
        sorted_teams = self.get_sorted_teams()
        
        if not sorted_teams:
            dpg.add_text("Aucune equipe a classer", parent="podium_container", color=[200, 200, 200])
            return
        
        # TITRE
        dpg.add_text("CLASSEMENT FINAL", parent="podium_container", color=[255, 215, 0])
        dpg.add_spacer(height=10, parent="podium_container")
        
        # PODIUM 3 PREMIERS
        with dpg.group(horizontal=True, parent="podium_container"):
            # 2eme
            if len(sorted_teams) > 1:
                with dpg.group(width=200):
                    dpg.add_text("2eme", color=[192, 192, 192])
                    dpg.add_text(f"EQUIPE {sorted_teams[1]['id']+1}", color=[200, 200, 200])
                    dpg.add_text(f"{sorted_teams[1]['score']} pts", color=[192, 192, 192])
            
            # 1er
            with dpg.group(width=250):
                dpg.add_text("1er", color=[255, 215, 0])
                dpg.add_text(f"EQUIPE {sorted_teams[0]['id']+1}", color=[255, 255, 255])
                dpg.add_text(f"{sorted_teams[0]['score']} pts", color=[255, 215, 0])
            
            # 3eme
            if len(sorted_teams) > 2:
                with dpg.group(width=200):
                    dpg.add_text("3eme", color=[205, 127, 50])
                    dpg.add_text(f"EQUIPE {sorted_teams[2]['id']+1}", color=[200, 200, 200])
                    dpg.add_text(f"{sorted_teams[2]['score']} pts", color=[205, 127, 50])
        
        dpg.add_spacer(height=20, parent="podium_container")
        dpg.add_separator(parent="podium_container")
        dpg.add_spacer(height=10, parent="podium_container")
        
        # CLASSEMENT COMPLET
        dpg.add_text("CLASSEMENT COMPLET", parent="podium_container", color=[100, 150, 255])
        dpg.add_spacer(height=5, parent="podium_container")
        
        for i, team in enumerate(sorted_teams):
            if i >= 3:
                with dpg.group(horizontal=True, parent="podium_container"):
                    dpg.add_text(f"{i+1}.", color=[200, 200, 200])
                    dpg.add_spacer(width=10)
                    dpg.add_text(f"Equipe {team['id']+1}", color=[200, 200, 200])
                    dpg.add_spacer(width=20)
                    dpg.add_text(f"{team['score']} pts", color=[150, 150, 255])
        
        dpg.add_spacer(height=15, parent="podium_container")
        dpg.add_separator(parent="podium_container")
        dpg.add_spacer(height=10, parent="podium_container")
        
        # STATISTIQUES
        total_score = sum(team["score"] for team in self.teams.values())
        avg_score = total_score / len(self.teams) if self.teams else 0
        
        dpg.add_text("STATISTIQUES", parent="podium_container", color=[100, 150, 255])
        dpg.add_spacer(height=5, parent="podium_container")
        dpg.add_text(f"Total: {total_score} pts", parent="podium_container")
        dpg.add_text(f"Moyenne: {avg_score:.1f} pts", parent="podium_container")
        dpg.add_text(f"Equipes: {len(self.teams)}", parent="podium_container")
        
        dpg.add_spacer(height=20, parent="podium_container")
        
        # BOUTONS
        with dpg.group(horizontal=True, parent="podium_container"):
            dpg.add_button(
                label="REINITIALISER",
                callback=lambda s, a: self.reset_game(),
                width=200,
                height=40
            )
            dpg.add_spacer(width=20)
            dpg.add_button(
                label="CONTINUER",
                callback=lambda s, a: self.continue_game(),
                width=200,
                height=40
            )
    
    def update_podium_display(self):
        if self.current_tab == "tab_podium" and dpg.does_item_exist("podium_container"):
            dpg.delete_item("podium_container", children_only=True)
            self.create_podium_display()
    
    # =====================================================
    # 2.6 COMMUNICATION SERIE
    # =====================================================
    
    def scan_serial_ports(self) -> List[Tuple[str, str]]:
        ports = []
        try:
            available_ports = serial.tools.list_ports.comports()
            
            for port in available_ports:
                device = port.device
                description = port.description.strip()
                vid = port.vid
                hwid = port.hwid.upper()
                
                is_arduino = False
                
                # Detection Arduino via VID connus
                if vid in [0x2341, 0x2A03, 0x1A86, 0x10C4, 0x0403]:
                    is_arduino = True
                
                desc_upper = description.upper()
                if any(k in desc_upper for k in ["ARDUINO", "CH340", "CP210", "FTDI", "UART", "USB SERIAL"]):
                    is_arduino = True
                
                if "VID_2341" in hwid or "VID_1A86" in hwid or "VID_10C4" in hwid:
                    is_arduino = True

                # Detection Mac : ports cu.usbserial et cu.usbmodem
                if device.startswith("/dev/cu.usbserial") or device.startswith("/dev/cu.usbmodem"):
                    is_arduino = True

                # Exclusions
                is_fake = False
                if "BLUETOOTH" in desc_upper:
                    is_fake = True
                # Sur Windows exclure COM1
                if device.upper() == "COM1":
                    is_fake = True
                # Sur Mac exclure ports internes
                if device.startswith("/dev/cu.Bluetooth") or device.startswith("/dev/cu.MALS"):
                    is_fake = True

                if is_arduino and not is_fake:
                    display_name = f"NANO - {description} ({device})"
                elif device.startswith("COM") or device.startswith("/dev/"):
                    display_name = f"PORT - {description} ({device})"
                else:
                    display_name = f"SYS - {description} ({device})"
                
                ports.append((device, display_name))
            
            ports.sort(key=lambda x: x[1])
            
        except Exception as e:
            self.add_log(f"Erreur scan ports: {e}", color=[255, 0, 0])
        
        return ports
    
    def _drain_serial_queues(self) -> None:
        while True:
            try:
                self._serial_line_queue.get_nowait()
            except queue.Empty:
                break
        while True:
            try:
                self._serial_ctrl_queue.get_nowait()
            except queue.Empty:
                break

    def connect_serial(self, port_name: str):
        if self._serial_connecting:
            self.add_log("Connexion deja en cours...", color=[255, 200, 0])
            return False
        self._serial_connecting = True
        self._drain_serial_queues()
        self.disconnect_serial(close_port_only=True)
        self.add_log(f"Connexion a {port_name}...", color=[255, 200, 0])
        if dpg.does_item_exist("btn_connect"):
            dpg.configure_item("btn_connect", enabled=False)
        threading.Thread(
            target=self._connect_serial_worker,
            args=(port_name,),
            daemon=True,
        ).start()
        return True

    def _connect_serial_worker(self, port_name: str):
        ser = None
        kind = "mega"
        try:
            ser = serial.Serial(
                port=port_name,
                baudrate=self.baud_rate,
                timeout=self.serial_timeout,
                write_timeout=1.0,
                dsrdtr=False,
                rtscts=False,
            )
            try:
                ser.dtr = False
                ser.rts = False
            except Exception:
                pass
            time.sleep(2.0)
            ser.reset_input_buffer()
            ser.write(b"WHO\n")
            ser.flush()
            deadline = time.time() + 1.5
            rx = b""
            kind = "mega"
            while time.time() < deadline:
                n = ser.in_waiting
                if n:
                    rx += ser.read(n)
                if b"READY_NANO_SON" in rx:
                    kind = "nano_son"
                    break
                if b"READY_MEGA" in rx:
                    kind = "mega"
                    break
                time.sleep(0.03)
            self._serial_ctrl_queue.put(("connect_ok", port_name, kind, ser, rx))
        except Exception as e:
            if ser is not None:
                try:
                    ser.close()
                except Exception:
                    pass
            self._serial_ctrl_queue.put(("connect_fail", str(e)))

    def _finish_serial_connect(
        self, port_name: str, kind: str, ser: serial.Serial, ident_rx: bytes = b""
    ) -> None:
        self._drain_serial_queues()
        self.serial_connection = ser
        self.serial_device_kind = kind
        self.serial_running = True
        self.serial_thread = threading.Thread(
            target=self._serial_listener_worker,
            daemon=True,
        )
        self.serial_thread.start()
        self.add_log(f"Connecte a {port_name}", color=[0, 255, 0])
        if ident_rx:
            try:
                preview = ident_rx.decode("utf-8", errors="replace").strip().replace("\r", " ")
                if preview:
                    self.add_log(f"Identification: {preview[:80]}", color=[160, 160, 180])
            except Exception:
                pass
        if kind == "nano_son":
            self.nano_audio_on_pc = True
            self.update_connection_status(f"NANO SON ({port_name})", True)
            self.add_log(
                "Nano son seul sur USB (Mega non branchée au PC) — radio Mega→Nano ; "
                "buzz FWD_SON:200, animateur CMD_SENT: ; sons PC par défaut.",
                color=[120, 200, 255],
            )
            self._serial_write_line(b"AUDIO_PC\n")
            if dpg.does_item_exist("chk_nano_pc_audio"):
                dpg.configure_item("chk_nano_pc_audio", show=True)
                dpg.set_value("chk_nano_pc_audio", True)
        else:
            self.update_connection_status(f"MEGA / TTL ({port_name})", True)
            if b"READY_MEGA" not in ident_rx:
                self.add_log(
                    "ATTENTION: pas de READY_MEGA — mauvais port ou firmware Mega ancien. "
                    "Utilisez TX3/RX3 (14/15) ou reflash megaf.ino.",
                    color=[255, 160, 80],
                )
            self.add_log(
                "Mega : cable USB-TTL sur TX3/RX3 (broches 14/15), 9600 — pas le port USB shield DMX.",
                color=[120, 200, 255],
            )
            self.add_log("En attente de BUZZ:n depuis la Mega...", color=[180, 180, 180])

    def disconnect_serial(self, close_port_only: bool = False):
        self.serial_running = False
        kind = getattr(self, "serial_device_kind", "mega")
        conn = self.serial_connection
        if conn and conn.is_open:
            try:
                if kind == "nano_son" and not close_port_only:
                    conn.write(b"AUDIO_DF\n")
                    conn.flush()
                    time.sleep(0.06)
            except Exception:
                pass
            try:
                conn.close()
            except Exception:
                pass
        self.serial_connection = None
        if close_port_only:
            return
        self.serial_device_kind = "mega"
        self.nano_audio_on_pc = False
        if dpg.does_item_exist("chk_nano_pc_audio"):
            dpg.configure_item("chk_nano_pc_audio", show=False)
        self.update_connection_status("DECONNECTE", False)
        self._serial_connecting = False
        if not close_port_only:
            self._drain_serial_queues()
        if dpg.does_item_exist("btn_connect"):
            dpg.configure_item("btn_connect", enabled=True)

    def pump_serial_queues(self):
        """A appeler depuis la boucle Dear PyGui (thread principal uniquement)."""
        while True:
            try:
                line = self._serial_line_queue.get_nowait()
            except queue.Empty:
                break
            self._process_serial_line(line)

        while True:
            try:
                evt = self._serial_ctrl_queue.get_nowait()
            except queue.Empty:
                break
            kind = evt[0]
            if kind == "disconnect":
                was_open = self.serial_connection is not None
                self.disconnect_serial()
                if was_open:
                    self.add_log("Port serie perdu — deconnecte", color=[255, 120, 80])
            elif kind == "connect_ok":
                _, port_name, device_kind, ser, ident_rx = evt
                self._finish_serial_connect(port_name, device_kind, ser, ident_rx)
                self._serial_connecting = False
                if dpg.does_item_exist("btn_connect"):
                    dpg.configure_item("btn_connect", enabled=True)
            elif kind == "connect_fail":
                _, msg = evt
                self._serial_connecting = False
                if dpg.does_item_exist("btn_connect"):
                    dpg.configure_item("btn_connect", enabled=True)
                self.add_log(f"Erreur connexion: {msg}", color=[255, 0, 0])

    def _serial_listener_worker(self):
        buffer = b""
        conn = self.serial_connection
        while self.serial_running and conn and conn.is_open:
            try:
                if conn.in_waiting > 0:
                    data = conn.read(conn.in_waiting)
                    if data:
                        buffer += data
                        while b"\n" in buffer:
                            line_end = buffer.find(b"\n")
                            line = buffer[:line_end].strip()
                            buffer = buffer[line_end + 1 :]
                            if line:
                                decoded = line.decode("utf-8", errors="ignore").strip()
                                if decoded:
                                    self._serial_line_queue.put(decoded)
                time.sleep(0.01)
            except Exception:
                break
        # Deconnexion volontaire : serial_running deja False → ne pas couper une nouvelle session
        if self.serial_running:
            self.serial_running = False
            self._serial_ctrl_queue.put(("disconnect",))
    
    def _process_serial_line(self, line: str):
        if not line:
            return

        if self._serial_log_rx and not line.startswith("CONF:"):
            if (
                line.startswith("BUZZ:")
                or line.startswith("BUZZ_EQUIPE:")
                or line.startswith("FWD_SON:")
                or "CMD_SENT:" in line
            ):
                self.add_log(f"<< {line}", color=[120, 200, 255])
        
        # Compatible BUZZ: et BUZZ_EQUIPE:
        if line.startswith("BUZZ:") or line.startswith("BUZZ_EQUIPE:"):
            try:
                parts = line.split(":", 1)
                team_num = int(parts[1].strip())
                team_id = team_num - 1
                if 0 <= team_id < len(self.teams):
                    self.handle_buzz(team_id)
                else:
                    self.add_log(
                        f"BUZZ equipe {team_num} ignoree ({len(self.teams)} equipes dans le logiciel). "
                        f"Aligner nb equipes (config Mega EEPROM ou +/- dans l'interface).",
                        color=[255, 160, 80],
                    )
            except (ValueError, IndexError):
                self.add_log(f"Ligne BUZZ illisible: {line}", color=[255, 100, 100])
        elif "CMD_SENT:RESET_ALL" in line or "BUTTON:VALIDER" in line:
            self.add_log("Bouton VALIDER", color=[0, 255, 0])
            if self.state == GameState.BUZZED and self.current_team is not None:
                self.handle_correct_answer(from_hardware=True)
            elif (
                getattr(self, "serial_device_kind", "mega") == "nano_son"
                and not self._audio_use_external_df()
                and PYGAME_OK
            ):
                self.jouer_son(self.son_victoire)
        elif "CMD_SENT:RELANCE_PARTIEL" in line or "BUTTON:FAUX" in line:
            self.add_log("Bouton FAUX", color=[255, 100, 0])
            if self.state == GameState.BUZZED:
                self.handle_wrong_answer(from_hardware=True)
            elif (
                getattr(self, "serial_device_kind", "mega") == "nano_son"
                and not self._audio_use_external_df()
                and PYGAME_OK
            ):
                self.jouer_son(self.son_echec)
        elif line.startswith("FWD_SON:"):
            self._play_fwd_son_line(line)
        elif self._serial_log_rx and len(line) < 100:
            self.add_log(f"<< {line}", color=[100, 100, 120])
    
    # =====================================================
    # 2.7 INTERFACE UTILISATEUR
    # =====================================================
    
    def create_dark_theme(self):
        with dpg.theme() as theme:
            with dpg.theme_component(dpg.mvAll):
                dpg.add_theme_color(dpg.mvThemeCol_WindowBg, (18, 18, 24))
                dpg.add_theme_color(dpg.mvThemeCol_ChildBg, (25, 25, 32))
                dpg.add_theme_color(dpg.mvThemeCol_Border, (60, 60, 80))
                dpg.add_theme_color(dpg.mvThemeCol_FrameBg, (40, 40, 50))
                dpg.add_theme_color(dpg.mvThemeCol_Text, (220, 220, 230))
                dpg.add_theme_color(dpg.mvThemeCol_Button, (45, 85, 145))
                dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (60, 110, 190))
                dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (40, 70, 120))
                dpg.add_theme_style(dpg.mvStyleVar_WindowRounding, 6)
                dpg.add_theme_style(dpg.mvStyleVar_FrameRounding, 4)
        
        self.theme_id = theme
        return theme
    
    def create_interface(self):
        self.create_dark_theme()
        
        with dpg.window(
            tag="main_window",
            label="QUIZ BOARD V6",
            width=1300,
            height=980,
            pos=[10, 10],
            no_collapse=True,
            no_close=True
        ):
            if self.theme_id:
                dpg.bind_item_theme("main_window", self.theme_id)
            
            # ========== EN-TETE ==========
            with dpg.group(horizontal=True):
                dpg.add_text("QUIZ BOARD V6", color=[0, 200, 255])
                dpg.add_spacer(width=20)
                dpg.add_text(f"Equipes: {len(self.teams)}/{self.MAX_TEAMS}", 
                            tag="txt_teams_count", color=[200, 200, 200])
                dpg.add_spacer(width=20)
                dpg.add_text(f"Etat: {self.state.value}", tag="txt_state", color=[100, 200, 100])
            
            dpg.add_spacer(height=5)
            
            # ========== CONNEXION ==========
            with dpg.group(horizontal=True):
                dpg.add_combo(
                    items=[],
                    tag="combo_serial_ports",
                    width=300,
                    default_value="Selectionnez port COM..."
                )
                dpg.add_button(label="Actualiser", callback=self.refresh_ports_callback, width=80)
                dpg.add_button(label="CONNECTER", tag="btn_connect", callback=self.connect_callback, width=100)
                dpg.add_text("", tag="txt_connection_status", color=[255, 50, 50])
            dpg.add_checkbox(
                tag="chk_nano_pc_audio",
                label="Nano : sons sur le PC (DF muet — la Mega continue la radio, un seul son)",
                default_value=True,
                show=False,
                callback=self.nano_pc_audio_callback,
            )
            
            dpg.add_spacer(height=5)

            # ========== CHRONO + PROJECTEUR ==========
            with dpg.group(horizontal=True):
                dpg.add_text("CHRONO:", color=[100, 150, 255])
                dpg.add_spacer(width=5)
                dpg.add_checkbox(
                    tag="chk_chrono_enabled",
                    label="Actif",
                    default_value=self.chrono_enabled,
                    callback=self.chrono_enabled_callback,
                )
                dpg.add_spacer(width=10)
                dpg.add_text("Duree (s):", color=[180, 180, 200])
                dpg.add_spacer(width=5)
                dpg.add_input_int(
                    tag="input_chrono_duration",
                    default_value=self.chrono_duration_sec,
                    min_value=self.CHRONO_MIN_SEC,
                    max_value=self.CHRONO_MAX_SEC,
                    step=5,
                    step_fast=15,
                    width=70,
                    callback=self.chrono_duration_input_callback,
                )
                dpg.add_spacer(width=10)
                dpg.add_checkbox(
                    tag="chk_chrono_timeout",
                    label="Temps ecoule = FAUX",
                    default_value=self.chrono_timeout_faux,
                    callback=self.chrono_timeout_callback,
                )
                dpg.add_spacer(width=15)
                dpg.add_button(
                    label="PUBLIC",
                    callback=self.toggle_public_display,
                    width=80,
                )

            dpg.add_spacer(height=5)
            
            # ========== CONTROLES EN UNE SEULE LIGNE ==========
            with dpg.group(horizontal=True):
                # Valeur question
                dpg.add_text("PTS:", color=[100, 150, 255])
                dpg.add_spacer(width=5)
                dpg.add_radio_button(
                    items=["1", "5", "10", "20", "50"],
                    default_value="1",
                    callback=self.question_value_callback,
                    horizontal=True
                )
                dpg.add_spacer(width=20)
                # Boutons gestion
                dpg.add_button(label="AJOUTER", callback=lambda s, a: self.add_team(), width=70)
                dpg.add_spacer(width=3)
                dpg.add_button(label="SUPPRIMER", callback=lambda s, a: self.remove_team(), width=75)
                dpg.add_spacer(width=3)
                dpg.add_button(label="RESET", callback=lambda s, a: self.reset_game(), width=55)
                dpg.add_spacer(width=3)
                dpg.add_button(label="FIN JEU", callback=lambda s, a: self.finish_game(), width=65)
                dpg.add_spacer(width=20)
                # Statut
                dpg.add_text("STATUT:", color=[200, 200, 200])
                dpg.add_spacer(width=5)
                dpg.add_text("PRET", tag="txt_game_status", color=[0, 255, 0])
                dpg.add_spacer(width=10)
                dpg.add_text("EQUIPE:", color=[200, 200, 200])
                dpg.add_spacer(width=5)
                dpg.add_text("", tag="txt_current_team", color=[255, 255, 0], show=False)

            dpg.add_spacer(height=5)

            # ========== ONGLETS ==========
            with dpg.group(horizontal=True):
                dpg.add_button(
                    label="SCORES",
                    tag="btn_tab_scores",
                    callback=lambda s, a: self.switch_tab("tab_scores"),
                    width=150,
                    height=28
                )
                dpg.add_spacer(width=5)
                dpg.add_button(
                    label="PODIUM",
                    tag="btn_tab_podium",
                    callback=lambda s, a: self.switch_tab("tab_podium"),
                    width=150,
                    height=28
                )
            
            dpg.add_spacer(height=5)
            
            # ========== ONGLET SCORES ==========
            with dpg.child_window(
                tag="tab_scores",
                height=700,
                border=True,
                show=True
            ):
                dpg.add_text("SCORES", color=[100, 150, 255])
                dpg.add_spacer(height=5)
                with dpg.child_window(
                    tag="teams_container",
                    height=680,
                    border=False,
                    horizontal_scrollbar=False
                ):
                    self.refresh_teams_display()
            
            # ========== ONGLET PODIUM ==========
            with dpg.child_window(
                tag="tab_podium",
                height=700,
                border=True,
                show=False
            ):
                with dpg.child_window(
                    tag="podium_container",
                    height=680,
                    border=False,
                    horizontal_scrollbar=True
                ):
                    self.create_podium_display()
            
            dpg.add_spacer(height=5)
            
            # ========== CONSOLE EN BAS ==========
            with dpg.group(horizontal=True):
                dpg.add_text("CONSOLE", color=[100, 150, 255])
                dpg.add_spacer(width=10)
                dpg.add_button(label="EFFACER", callback=self.reset_console, width=70)
            
            with dpg.child_window(tag="log_window", height=80, border=True):
                dpg.add_text("", tag="txt_console_logs", wrap=0)

    def create_projector_display(self):
        """Écran public : buzz (équipe + chrono) ou podium final en grand."""
        with dpg.theme() as projector_theme:
            with dpg.theme_component(dpg.mvAll):
                dpg.add_theme_color(dpg.mvThemeCol_WindowBg, (4, 4, 8))
                dpg.add_theme_color(dpg.mvThemeCol_Text, (240, 240, 245))
                dpg.add_theme_color(dpg.mvThemeCol_Border, (4, 4, 8))
                dpg.add_theme_color(dpg.mvThemeCol_ChildBg, (4, 4, 8))

        pw = int(self.config.get("projector_width", 1920))
        ph = int(self.config.get("projector_height", 1080))

        with dpg.window(
            tag="projector_window",
            label="",
            width=pw,
            height=ph,
            show=False,
            no_title_bar=True,
            no_resize=True,
            no_move=False,
            no_collapse=True,
            no_close=True,
            no_background=False,
        ):
            dpg.bind_item_theme("projector_window", projector_theme)
            with dpg.child_window(
                tag="proj_canvas",
                border=False,
                width=-1,
                height=-1,
                no_scrollbar=True,
            ):
                with dpg.group(tag="proj_view_buzz", show=False):
                    dpg.add_spacer(height=max(60, ph // 7))
                    with dpg.group(horizontal=True):
                        dpg.add_spacer(width=max(80, pw // 10))
                        dpg.add_text("", tag="txt_proj_team", color=[80, 180, 255])
                    dpg.add_spacer(height=max(20, ph // 20))
                    with dpg.group(horizontal=True):
                        dpg.add_spacer(width=max(100, pw // 8))
                        dpg.add_text("", tag="txt_proj_ecoute", color=[100, 220, 160])
                    dpg.add_spacer(height=max(30, ph // 15))
                    with dpg.group(horizontal=True):
                        dpg.add_spacer(width=max(100, pw // 8))
                        dpg.add_text("", tag="txt_proj_points", color=[255, 215, 80])
                    dpg.add_spacer(height=max(40, ph // 12))
                    with dpg.group(horizontal=True):
                        dpg.add_spacer(width=max(60, pw // 10))
                        dpg.add_text("", tag="txt_proj_timer", color=[255, 255, 255])

                with dpg.group(tag="proj_view_podium", show=False):
                    with dpg.child_window(
                        tag="proj_podium_container",
                        border=False,
                        width=-1,
                        height=-1,
                        no_scrollbar=True,
                    ):
                        pass

        font_map = [
            ("txt_proj_team", self.font_proj_team),
            ("txt_proj_ecoute", self.font_proj_ecoute),
            ("txt_proj_points", self.font_proj_points),
            ("txt_proj_timer", self.font_proj_timer),
        ]
        for tag, font_id in font_map:
            if font_id and dpg.does_item_exist(tag):
                dpg.bind_item_font(tag, font_id)

    def bind_projector_fonts(self):
        """À appeler après création du font registry (dans main)."""
        font_path = find_bold_ui_font()
        if not font_path:
            return
        with dpg.font_registry():
            self.font_proj_timer = dpg.add_font(font_path, 200)
            self.font_proj_team = dpg.add_font(font_path, 96)
            self.font_proj_ecoute = dpg.add_font(font_path, 56)
            self.font_proj_points = dpg.add_font(font_path, 72)
            self.font_proj_podium_title = dpg.add_font(font_path, 64)
            self.font_proj_podium_name = dpg.add_font(font_path, 80)
            self.font_proj_podium_score = dpg.add_font(font_path, 48)
    
    def refresh_teams_display(self):
        if dpg.does_item_exist("teams_container"):
            dpg.delete_item("teams_container", children_only=True)
            
            if dpg.does_item_exist("txt_teams_count"):
                dpg.set_value("txt_teams_count", f"Equipes: {len(self.teams)}/{self.MAX_TEAMS}")
            
            nb_teams = len(self.teams)

            # Responsive : adapte colonnes et largeur
            if nb_teams <= 2:
                nb_columns = 2
                card_width = 600
            elif nb_teams <= 4:
                nb_columns = 2
                card_width = 600
            elif nb_teams <= 6:
                nb_columns = 3
                card_width = 390
            elif nb_teams <= 8:
                nb_columns = 4
                card_width = 290
            else:
                nb_columns = 4
                card_width = 290
            
            teams_list = sorted(self.teams.keys())
            
            for i in range(0, nb_teams, nb_columns):
                with dpg.group(horizontal=True, parent="teams_container"):
                    for j in range(nb_columns):
                        idx = i + j
                        if idx < nb_teams:
                            self.create_team_card(teams_list[idx], card_width)
    
    def create_team_card(self, team_id: int, card_width: int = 290):
        if team_id not in self.teams:
            return
        
        team = self.teams[team_id]
        
        with dpg.child_window(width=card_width, height=95, border=True):
            # En-tete
            with dpg.group(horizontal=True):
                dpg.add_text(f"EQ {team_id+1}", color=[0, 200, 255])
                dpg.add_spacer(width=card_width-110)
                dpg.add_text(f"{team['score']} pts",
                            tag=f"team_score_{team_id}",
                            color=[255, 255, 0])
            
            dpg.add_spacer(height=3)
            
            # Boutons + - et BUZZ
            with dpg.group(horizontal=True):
                dpg.add_button(label="+", callback=self.plus_score_callback, user_data=team_id, width=30, height=22)
                dpg.add_spacer(width=3)
                dpg.add_button(label="-", callback=self.minus_score_callback, user_data=team_id, width=30, height=22)
                dpg.add_spacer(width=8)
                dpg.add_text("BUZZ", tag=f"buzz_indicator_{team_id}", show=False, color=[255, 0, 0])
            
            dpg.add_spacer(height=3)
            
            # Boutons VALIDER/FAUX
            with dpg.group(horizontal=True, tag=f"validation_group_{team_id}", show=False):
                dpg.add_button(label="VALIDER", callback=self.correct_answer_callback, user_data=team_id, width=75, height=22)
                dpg.add_spacer(width=3)
                dpg.add_button(label="FAUX", callback=self.wrong_answer_callback, user_data=team_id, width=55, height=22)
    
    def update_team_display(self, team_id: int):
        if team_id in self.teams:
            score_tag = f"team_score_{team_id}"
            if dpg.does_item_exist(score_tag):
                dpg.set_value(score_tag, f"{self.teams[team_id]['score']} pts")
    
    def update_game_display(self):
        if dpg.does_item_exist("txt_state"):
            dpg.set_value("txt_state", f"Etat: {self.state.value}")
        
        if self.state == GameState.IDLE:
            dpg.set_value("txt_game_status", "PRET")
            dpg.configure_item("txt_game_status", color=[0, 255, 0])
            dpg.configure_item("txt_current_team", show=False)
            
            for team_id in self.teams:
                buzz_tag = f"buzz_indicator_{team_id}"
                if dpg.does_item_exist(buzz_tag):
                    dpg.configure_item(buzz_tag, show=False)
        
        elif self.state == GameState.BUZZED and self.current_team is not None:
            dpg.set_value("txt_game_status", "BUZZE")
            dpg.configure_item("txt_game_status", color=[255, 255, 0])
            dpg.set_value("txt_current_team", f"Equipe {self.current_team+1}")
            dpg.configure_item("txt_current_team", show=True)
            
            for team_id in self.teams:
                buzz_tag = f"buzz_indicator_{team_id}"
                if dpg.does_item_exist(buzz_tag):
                    dpg.configure_item(buzz_tag, show=(team_id == self.current_team))
        
        elif self.state == GameState.FINISHED:
            dpg.set_value("txt_game_status", "TERMINE")
            dpg.configure_item("txt_game_status", color=[255, 215, 0])
            dpg.configure_item("txt_current_team", show=False)
    
    def show_validation_buttons(self, team_id: int, show: bool = True):
        tag = f"validation_group_{team_id}"
        if dpg.does_item_exist(tag):
            dpg.configure_item(tag, show=show)
    
    def hide_all_validation_buttons(self):
        for team_id in self.teams:
            tag = f"validation_group_{team_id}"
            if dpg.does_item_exist(tag):
                dpg.configure_item(tag, show=False)
    
    # =====================================================
    # 2.8 CALLBACKS GENERAUX + CONSOLE AUTO-NETTOYANTE
    # =====================================================
    
    def switch_tab(self, tab_name: str):
        self.current_tab = tab_name
        if tab_name == "tab_scores":
            dpg.hide_item("tab_podium")
            dpg.show_item("tab_scores")
        else:
            dpg.hide_item("tab_scores")
            dpg.show_item("tab_podium")
            self.update_podium_display()
    
    def refresh_ports_callback(self, sender, app_data):
        ports = self.scan_serial_ports()
        if ports:
            display_names = [name for _, name in ports]
            dpg.configure_item("combo_serial_ports", items=display_names)
            self.add_log(f"{len(ports)} ports detectes", color=[0, 255, 0])
        else:
            dpg.configure_item("combo_serial_ports", items=["AUCUN PORT"])
            self.add_log("Aucun port detecte", color=[255, 0, 0])
    
    def connect_callback(self, sender, app_data):
        selected = dpg.get_value("combo_serial_ports")
        if not selected or selected in ["Selectionnez port COM...", "AUCUN PORT"]:
            self.add_log("Selectionnez un port", color=[255, 150, 0])
            return

        if self._serial_connecting:
            self.add_log("Connexion en cours, patientez...", color=[255, 200, 0])
            return
        
        if self.serial_connection and self.serial_connection.is_open:
            self.disconnect_serial()
            self.add_log("Deconnecte", color=[200, 200, 200])
        else:
            port_name = None

            # Windows : COM3, COM4...
            match_win = re.search(r"\((COM\d+)\)", selected)
            if match_win:
                port_name = match_win.group(1)

            # Mac : /dev/cu.usbserial-XXXX ou /dev/cu.usbmodem-XXXX
            match_mac = re.search(r"\((/dev/cu\.[^\)]+)\)", selected)
            if match_mac:
                port_name = match_mac.group(1)

            # Linux : /dev/ttyUSB0 ou /dev/ttyACM0
            match_linux = re.search(r"\((/dev/tty[^\)]+)\)", selected)
            if match_linux:
                port_name = match_linux.group(1)

            if port_name:
                self.connect_serial(port_name)
            else:
                self.add_log("Port non reconnu", color=[255, 0, 0])
    
    def update_connection_status(self, status: str, connected: bool):
        if dpg.does_item_exist("txt_connection_status"):
            dpg.set_value("txt_connection_status", status)
            color = [0, 255, 0] if connected else [255, 50, 50]
            dpg.configure_item("txt_connection_status", color=color)
        if dpg.does_item_exist("btn_connect"):
            dpg.configure_item("btn_connect", 
                              label="DECONNECTER" if connected else "CONNECTER")
    
    def question_value_callback(self, sender, app_data):
        try:
            self.question_value = int(app_data)
            self.add_log(f"Valeur: {self.question_value} pts", color=[100, 200, 255])
            if self.state == GameState.BUZZED:
                self.update_projector_display()
        except (TypeError, ValueError):
            pass

    def nano_pc_audio_callback(self, sender, app_data):
        """Nano connecte : bascule AUDIO_PC (pygame + FWD_SON) / AUDIO_DF (lecteur SD)."""
        use_pc = bool(app_data)
        self.nano_audio_on_pc = use_pc
        self._serial_write_line(b"AUDIO_PC\n" if use_pc else b"AUDIO_DF\n")
        if use_pc:
            self.add_log("Sons sur le PC — DFPlayer muet sur le Nano", color=[100, 220, 180])
        else:
            self.add_log("Sons sur DFPlayer (carte SD)", color=[255, 200, 120])
    
    # ========== FONCTION DE LOG AUTO-NETTOYANTE ==========
    def add_log(self, message: str, color: Tuple[int, int, int] = (200, 200, 200)):
        """Ajoute un message au journal et garde seulement les 10 dernieres lignes."""
        if dpg.does_item_exist("txt_console_logs"):
            timestamp = datetime.now().strftime("%H:%M:%S")
            formatted = f"[{timestamp}] {message}"
            self.log_lines.append(formatted)
            full_text = "\n".join(self.log_lines)
            dpg.set_value("txt_console_logs", full_text)
            dpg.set_y_scroll("log_window", -1.0)

    def reset_console(self, sender=None, app_data=None):
        """Efface tous les messages de la console."""
        self.log_lines.clear()
        if dpg.does_item_exist("txt_console_logs"):
            dpg.set_value("txt_console_logs", "")

# =========================================================
# 3. POINT D'ENTREE
# =========================================================

def main():
    dpg.create_context()
    
    controller = QuizController()
    controller.bind_projector_fonts()
    controller.create_interface()
    controller.create_projector_display()
    
    controller.add_log("QUIZ BOARD V6 - Demarrage", color=[0, 200, 255])
    controller.add_log(f"{len(controller.teams)} equipes", color=[100, 255, 100])
    if not PYGAME_OK:
        controller.add_log("Pygame absent — pas de sons PC (pip install pygame).", color=[255, 140, 80])
    elif controller.son_buzz is None:
        controller.add_log(f"Pas de buzz.mp3 — placer les MP3 dans: {controller.sounds_dir}", color=[255, 180, 100])

    bp = int(controller.config.get("points_au_buzz", 0) or 0)
    if bp > 0:
        controller.add_log(
            f"Points au buzz : +{bp} pt(s) par equipe qui buzze (quiz_board_config.json → points_au_buzz).",
            color=[160, 220, 255],
        )
    if controller.chrono_enabled:
        controller.add_log(
            f"Chrono public : {controller.chrono_duration_sec} s — bouton PUBLIC ouvre le 2e ecran "
            f"(mode Etendu Windows/Linux, pas Dupliquer).",
            color=[160, 220, 255],
        )

    ports = controller.scan_serial_ports()
    if ports:
        display_names = [name for _, name in ports]
        dpg.configure_item("combo_serial_ports", items=display_names)

    dpg.create_viewport(
        title="QUIZ BOARD V6",
        width=1320,
        height=1020,
        resizable=True,
        vsync=True,
    )
    
    dpg.setup_dearpygui()
    # Obligatoire : avant ``show_viewport`` (doc Dear PyGui)
    apply_viewport_icons()
    dpg.show_viewport()
    dpg.set_primary_window("main_window", True)
    
    last_auto_save_check = time.time()
    
    try:
        while dpg.is_dearpygui_running():
            controller.pump_serial_queues()
            controller.tick_chrono()
            dpg.render_dearpygui_frame()
            
            current_time = time.time()
            if controller.save_pending and current_time - last_auto_save_check > controller.auto_save_interval:
                if controller.save_config():
                    controller.save_pending = False
                    controller.last_save_time = current_time
                    controller.add_log("Auto-save OK", color=[100, 200, 100])
                last_auto_save_check = current_time
            
            time.sleep(0.001)
    
    except KeyboardInterrupt:
        print("Arret")
    except Exception as e:
        print(f"Erreur: {e}")
    finally:
        controller.close_public_display()
        controller.disconnect_serial()
        controller.save_config()
        dpg.destroy_context()

if __name__ == "__main__":
    main()