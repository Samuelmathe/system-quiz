"""
QUIZ BOARD V6 - Version finale
- Icône fenêtre / barre titre : dossier projet ``assets/`` (score.ico puis score.png)
- Console auto-nettoyante (garde les 10 dernières lignes)
"""

import dearpygui.dearpygui as dpg
import serial
import serial.tools.list_ports
import json
import threading
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
except:
    PYGAME_OK = False

def get_base_path():
    """Retourne le chemin du dossier où se trouve l'exe ou le script.
    Utilise sys.executable pour accéder aux fichiers EXTERNES (sounds/)
    placés à côté de l'exécutable par le client."""
    if getattr(sys, 'frozen', False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(__file__))


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
        
        # Communication serie
        self.serial_connection: Optional[serial.Serial] = None
        self.serial_running = False
        self.serial_thread: Optional[threading.Thread] = None
        
        # Interface
        self.theme_id: Optional[str] = None
        self.current_tab = "tab_scores"
        
        # Auto-save
        self.last_save_time = time.time()
        self.save_pending = False
        
        # Constantes
        self.MIN_TEAMS = 1
        self.MAX_TEAMS = 30
        
        # ========== GESTION DE LA CONSOLE ==========
        self.log_lines = deque(maxlen=10)  # Garde seulement 10 lignes
        
        # ========== SONS ==========
        self.son_buzz     = None
        self.son_victoire = None
        self.son_echec    = None
        self.sons_equipes = {}  # {team_id: pygame.Sound}
        self._charger_sons()
        
        # Initialisation
        self.load_config()
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
            sounds_dir = os.path.join(self.base_path, "sounds")
            
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

    def _recharger_sons_equipes(self):
        """Recharge uniquement les sons équipes (utile si le client ajoute des fichiers)"""
        if not PYGAME_OK:
            return
        sounds_dir = os.path.join(self.base_path, "sounds")
        self.sons_equipes = {}
        for i in range(1, 31):
            son_path = os.path.join(sounds_dir, f"equipe_{i}.mp3")
            if os.path.exists(son_path):
                try:
                    self.sons_equipes[i-1] = pygame.mixer.Sound(son_path)
                except:
                    pass

    def arreter_sons(self):
        """Coupe tous les sons (canaux) — utile quand buzz équipe passe à victoire/échec."""
        if not PYGAME_OK:
            return
        try:
            pygame.mixer.stop()
        except Exception:
            pass
    
    def jouer_son(self, son):
        """Joue un son en arrière plan sans bloquer l'interface"""
        if not PYGAME_OK or son is None:
            return
        try:
            threading.Thread(target=son.play, daemon=True).start()
        except:
            pass
    
    def load_config(self):
        default_config = {
            "nb_equipes": 8,
            "auto_save": True,
            "last_scores": {}
        }
        
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, "r", encoding='utf-8') as f:
                    loaded = json.load(f)
                    default_config.update(loaded)
            except Exception as e:
                print(f"Erreur chargement config: {e}")
        
        self.config = default_config
    
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
            self.handle_correct_answer()
    
    def wrong_answer_callback(self, sender, app_data, user_data):
        team_id = user_data
        if self.state == GameState.BUZZED and self.current_team == team_id:
            self.handle_wrong_answer()
    
    # =====================================================
    # 2.4 LOGIQUE DE JEU
    # =====================================================
    
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
        
        # Son buzz : son spécifique équipe si disponible, sinon son général
        if team_id in self.sons_equipes:
            self.jouer_son(self.sons_equipes[team_id])
        else:
            self.jouer_son(self.son_buzz)
        
        self.update_game_display()
        self.show_validation_buttons(team_id, True)
        
        self.add_log(f"Equipe {team_id+1} a buzze !", color=[255, 255, 0])
    
    def handle_correct_answer(self):
        if self.state != GameState.BUZZED or self.current_team is None:
            return
        
        team_id = self.current_team
        self.update_team_score(team_id, self.question_value)
        self.add_log(f"Bonne reponse ! +{self.question_value} pts Equipe {team_id+1}", 
                    color=[0, 255, 0])
        
        # Arrêter le buzz / son équipe en cours puis jouer la victoire
        self.arreter_sons()
        self.jouer_son(self.son_victoire)
        
        if self.serial_connection and self.serial_connection.is_open:
            try:
                self.serial_connection.write(b"RESET_ALL\n")
            except:
                pass
        
        self.reset_question()
    
    def handle_wrong_answer(self):
        if self.state != GameState.BUZZED:
            return
        
        self.add_log(f"Mauvaise reponse Equipe {self.current_team+1}", color=[255, 100, 0])
        
        # Arrêter le buzz / son équipe en cours puis jouer l'échec
        self.arreter_sons()
        self.jouer_son(self.son_echec)
        
        if self.serial_connection and self.serial_connection.is_open:
            try:
                self.serial_connection.write(b"RELANCE_PARTIEL\n")
            except:
                pass
        
        self.state = GameState.IDLE
        self.current_team = None
        self.hide_all_validation_buttons()
        self.update_game_display()
    
    def reset_question(self):
        self.state = GameState.IDLE
        self.current_team = None
        self.banned_teams.clear()
        self.hide_all_validation_buttons()
        self.update_game_display()
    
    def reset_game(self):
        self.state = GameState.IDLE
        self.game_finished = False
        self.current_team = None
        self.banned_teams.clear()
        self.hide_all_validation_buttons()
        
        for team_id in self.teams:
            self.teams[team_id]["score"] = 0
            self.update_team_display(team_id)
        
        if self.serial_connection and self.serial_connection.is_open:
            try:
                self.serial_connection.write(b"RESET_ALL\n")
            except:
                pass
        
        self.switch_tab("tab_scores")
        self.add_log("Jeu reinitialise", color=[200, 200, 0])
    
    def finish_game(self):
        self.state = GameState.FINISHED
        self.game_finished = True
        self.switch_tab("tab_podium")
        self.add_log("Fin du jeu - Classement final", color=[255, 215, 0])
    
    def continue_game(self):
        self.state = GameState.IDLE
        self.game_finished = False
        self.current_team = None
        self.banned_teams.clear()
        self.hide_all_validation_buttons()
        self.update_game_display()
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
            if dpg.does_item_exist("podium_container"):
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
    
    def connect_serial(self, port_name: str):
        self.disconnect_serial()
        
        try:
            self.add_log(f"Connexion a {port_name}...", color=[255, 200, 0])
            
            self.serial_connection = serial.Serial(
                port=port_name,
                baudrate=self.baud_rate,
                timeout=self.serial_timeout,
                write_timeout=1.0
            )
            
            time.sleep(2.0)
            self.serial_connection.reset_input_buffer()

            # Connexion directe sans vérification IDENT
            self.serial_running = True
            self.serial_thread = threading.Thread(
                target=self._serial_listener_worker,
                daemon=True
            )
            self.serial_thread.start()
            self.add_log(f"Connecte a {port_name}", color=[0, 255, 0])
            self.update_connection_status(f"CONNECTE ({port_name})", True)
            return True
                
        except Exception as e:
            self.add_log(f"Erreur connexion: {e}", color=[255, 0, 0])
            return False
    
    def disconnect_serial(self):
        self.serial_running = False
        if self.serial_connection:
            try:
                self.serial_connection.close()
            except:
                pass
            self.serial_connection = None
        self.update_connection_status("DECONNECTE", False)
    
    def _serial_listener_worker(self):
        buffer = b""
        while self.serial_running and self.serial_connection:
            try:
                if self.serial_connection.in_waiting > 0:
                    data = self.serial_connection.read(self.serial_connection.in_waiting)
                    if data:
                        buffer += data
                        while b'\n' in buffer:
                            line_end = buffer.find(b'\n')
                            line = buffer[:line_end].strip()
                            buffer = buffer[line_end + 1:]
                            if line:
                                try:
                                    decoded = line.decode('utf-8', errors='ignore').strip()
                                    self._process_serial_line(decoded)
                                except:
                                    pass
                time.sleep(0.01)
            except:
                time.sleep(1)
        self.disconnect_serial()
    
    def _process_serial_line(self, line: str):
        if not line:
            return
        
        # Compatible BUZZ: et BUZZ_EQUIPE:
        if line.startswith("BUZZ:") or line.startswith("BUZZ_EQUIPE:"):
            try:
                team_id = int(line.split(":")[1]) - 1
                if 0 <= team_id < len(self.teams):
                    self.handle_buzz(team_id)
            except:
                pass
        elif "CMD_SENT:RESET_ALL" in line or "BUTTON:VALIDER" in line:
            self.add_log("Bouton VALIDER", color=[0, 255, 0])
            if self.state == GameState.BUZZED and self.current_team is not None:
                self.handle_correct_answer()
        elif "CMD_SENT:RELANCE_PARTIEL" in line or "BUTTON:FAUX" in line:
            self.add_log("Bouton FAUX", color=[255, 100, 0])
            if self.state == GameState.BUZZED:
                self.handle_wrong_answer()
    
    # =====================================================
    # 2.7 INTERFACE UTILISATEUR
    # =====================================================
    
    def create_dark_theme(self):
        with dpg.theme() as theme:
            with dpg.theme_component(dpg.mvAll):
                # Fond noir / gris très sombre par défaut (toute l’app hérite via bind_theme)
                dpg.add_theme_color(dpg.mvThemeCol_Text, (230, 230, 238))
                dpg.add_theme_color(dpg.mvThemeCol_TextDisabled, (120, 120, 130))
                dpg.add_theme_color(dpg.mvThemeCol_WindowBg, (12, 12, 14))
                dpg.add_theme_color(dpg.mvThemeCol_ChildBg, (18, 18, 22))
                dpg.add_theme_color(dpg.mvThemeCol_PopupBg, (20, 20, 26))
                dpg.add_theme_color(dpg.mvThemeCol_MenuBarBg, (14, 14, 17))
                dpg.add_theme_color(dpg.mvThemeCol_TitleBg, (12, 12, 14))
                dpg.add_theme_color(dpg.mvThemeCol_TitleBgActive, (22, 22, 28))
                dpg.add_theme_color(dpg.mvThemeCol_Border, (48, 48, 62))
                dpg.add_theme_color(dpg.mvThemeCol_BorderShadow, (0, 0, 0))
                dpg.add_theme_color(dpg.mvThemeCol_FrameBg, (35, 35, 44))
                dpg.add_theme_color(dpg.mvThemeCol_FrameBgHovered, (42, 42, 54))
                dpg.add_theme_color(dpg.mvThemeCol_FrameBgActive, (48, 48, 62))
                dpg.add_theme_color(dpg.mvThemeCol_Header, (40, 40, 54))
                dpg.add_theme_color(dpg.mvThemeCol_HeaderHovered, (52, 52, 70))
                dpg.add_theme_color(dpg.mvThemeCol_HeaderActive, (62, 62, 85))
                dpg.add_theme_color(dpg.mvThemeCol_Button, (45, 85, 145))
                dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (60, 110, 190))
                dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (40, 70, 120))
                dpg.add_theme_color(dpg.mvThemeCol_ScrollbarBg, (16, 16, 20))
                dpg.add_theme_color(dpg.mvThemeCol_ScrollbarGrab, (55, 55, 72))
                dpg.add_theme_color(dpg.mvThemeCol_ScrollbarGrabHovered, (72, 72, 92))
                dpg.add_theme_color(dpg.mvThemeCol_ScrollbarGrabActive, (92, 92, 115))
                dpg.add_theme_color(dpg.mvThemeCol_CheckMark, (130, 200, 255))
                dpg.add_theme_style(dpg.mvStyleVar_WindowRounding, 6)
                dpg.add_theme_style(dpg.mvStyleVar_FrameRounding, 4)
        
        self.theme_id = theme
        return theme
    
    def create_interface(self):
        self.create_dark_theme()
        # Thème sombre comme défaut Dear PyGui (listes déroulantes, tooltips, éléments hors fenêtre principale)
        if self.theme_id:
            dpg.bind_theme(self.theme_id)
        
        with dpg.window(
            tag="main_window",
            label="QUIZ BOARD V6",
            width=1300,
            height=980,
            pos=[10, 10],
            no_collapse=True,
            no_close=True
        ):
            
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
        except:
            pass
    
    # ========== FONCTION DE LOG AUTO-NETTOYANTE ==========
    def add_log(self, message: str, color: Tuple[int, int, int] = (200, 200, 200)):
        """Ajoute un message au journal et garde seulement les 10 dernieres lignes."""
        if dpg.does_item_exist("txt_console_logs"):
            timestamp = datetime.now().strftime("%H:%M:%S")
            formatted = f"[{timestamp}] {message}"
            self.log_lines.append(formatted)
            full_text = "\n".join(list(self.log_lines))
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
    controller.create_interface()
    
    controller.add_log("QUIZ BOARD V6 - Demarrage", color=[0, 200, 255])
    controller.add_log(f"{len(controller.teams)} equipes", color=[100, 255, 100])
    
    ports = controller.scan_serial_ports()
    if ports:
        display_names = [name for _, name in ports]
        dpg.configure_item("combo_serial_ports", items=display_names)
    
    dpg.create_viewport(
        title='QUIZ BOARD V6',
        width=1320,
        height=1020,
        resizable=True,
        vsync=True
    )
    
    dpg.setup_dearpygui()
    # Obligatoire : avant ``show_viewport`` (doc Dear PyGui)
    apply_viewport_icons()
    dpg.show_viewport()
    dpg.set_primary_window("main_window", True)
    
    last_auto_save_check = time.time()
    
    try:
        while dpg.is_dearpygui_running():
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
        controller.disconnect_serial()
        controller.save_config()
        dpg.destroy_context()

if __name__ == "__main__":
    main()