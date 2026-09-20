/**
 * ==========================================================================
 * APP-COMMON.JS - Moteur partagé (WebSocket, Synthèse Audio, État & Sync)
 * ==========================================================================
 */

(function (window) {
  "use strict";

  // Clés de stockage
  const STORAGE_KEY_STATE = "quiz_dmx_state_v2";
  const STORAGE_KEY_IP = "quiz_esp32_ip";

  // État initial par défaut
  const defaultState = {
    connected: false,
    state: "IDLE", // IDLE | BUZZED | FINISHED
    currentTeam: null, // index 0..29
    pointsCourants: 1,
    pointsAuBuzz: 0,
    bannedTeams: [],
    chrono: {
      enabled: true,
      durationSec: 30,
      remaining: 30,
      running: false,
      timeoutFaux: true
    },
    equipes: [
      { id: 0, nom: "Équipe 1", score: 0, couleursUI: ["#EF4444", "#3B82F6"], couleurs: ["#EF4444", "#EF4444"], strobeDureeMs: 400 },
      { id: 1, nom: "Équipe 2", score: 0, couleursUI: ["#3B82F6", "#06B6D4"], couleurs: ["#3B82F6", "#3B82F6"], strobeDureeMs: 400 },
      { id: 2, nom: "Équipe 3", score: 0, couleursUI: ["#10B981", "#3B82F6"], couleurs: ["#10B981", "#10B981"], strobeDureeMs: 0 },
      { id: 3, nom: "Équipe 4", score: 0, couleursUI: ["#F59E0B", "#EF4444"], couleurs: ["#F59E0B", "#F59E0B"], strobeDureeMs: 0 },
      { id: 4, nom: "Équipe 5", score: 0, couleursUI: ["#EC4899", "#8B5CF6"], couleurs: ["#EC4899", "#EC4899"], strobeDureeMs: -1 },
      { id: 5, nom: "Équipe 6", score: 0, couleursUI: ["#06B6D4", "#10B981"], couleurs: ["#06B6D4", "#06B6D4"], strobeDureeMs: 0 }
    ],
    projecteurs: [
      { id: 1, adresse: 51, nbCanaux: 8, mode: 0, offDim: 0, offR: 1, offG: 2, offB: 3, offStrobe: -1, strobeValue: 200, strobeRepos: 0 },
      { id: 2, adresse: 59, nbCanaux: 17, mode: 1, offDim: 4, offR: 8, offG: 2, offB: 3, offStrobe: 5, strobeValue: 100, strobeRepos: 255 }
    ],
    questions: [],
    currentQuestionIndex: 0
  };

  // --- Gestionnaire de sons personnalises (charges par le client depuis son
  // appareil, meme convention de noms que interface/sounds/ cote logiciel PC :
  // buzz.mp3, victoire.mp3, echec.mp3, equipe_N.mp3). Les fichiers sont
  // conserves dans IndexedDB (sur l'appareil lui-meme, pas sur l'ESP32 dont
  // la flash est trop petite) et restaures a chaque ouverture de page. Les
  // elements Audio sont precharges une fois : jouer un son ne redecode plus
  // le fichier a chaque buzz. ---
  const SONS_DB_NOM = "dmx_quiz_sons";
  const SONS_DB_STORE = "sons";

  class SoundManager {
    constructor() {
      this.urls = { buzz: null, victoire: null, echec: null, equipes: {} };
      this.audios = {};
      // Promesse resolue avec la liste des slots restaures (vide si IndexedDB
      // indisponible : le mode "en memoire seulement" reste alors valable).
      this.pret = this._restaurer();
    }

    _ouvrirDB() {
      return new Promise((resolve, reject) => {
        if (!("indexedDB" in window)) return reject(new Error("IndexedDB indisponible"));
        const req = indexedDB.open(SONS_DB_NOM, 1);
        req.onupgradeneeded = () => req.result.createObjectStore(SONS_DB_STORE);
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
      });
    }

    _sauver(cle, blob) {
      return this._ouvrirDB().then((db) => new Promise((resolve, reject) => {
        const tx = db.transaction(SONS_DB_STORE, "readwrite");
        tx.objectStore(SONS_DB_STORE).put(blob, cle);
        tx.oncomplete = () => { db.close(); resolve(); };
        tx.onerror = () => { db.close(); reject(tx.error); };
      })).catch(() => {});
    }

    _restaurer() {
      return this._ouvrirDB().then((db) => new Promise((resolve, reject) => {
        const store = db.transaction(SONS_DB_STORE, "readonly").objectStore(SONS_DB_STORE);
        const reqCles = store.getAllKeys();
        const reqVals = store.getAll();
        reqVals.onsuccess = () => {
          const cles = reqCles.result;
          cles.forEach((cle, i) => this._appliquer(cle, reqVals.result[i]));
          db.close();
          resolve(cles);
        };
        reqVals.onerror = () => { db.close(); reject(reqVals.error); };
      })).catch(() => []);
    }

    // Associe un blob a son slot ("buzz", "victoire", "echec", "equipe_N") et
    // precharge l'element Audio correspondant.
    _appliquer(cle, blob) {
      const url = URL.createObjectURL(blob);
      const matchEquipe = String(cle).match(/^equipe_(\d+)$/);
      let ancien = null;
      if (cle === "buzz" || cle === "victoire" || cle === "echec") {
        ancien = this.urls[cle];
        this.urls[cle] = url;
      } else if (matchEquipe) {
        const idx = parseInt(matchEquipe[1], 10) - 1;
        ancien = this.urls.equipes[idx];
        this.urls.equipes[idx] = url;
      } else {
        URL.revokeObjectURL(url);
        return;
      }
      if (ancien) {
        URL.revokeObjectURL(ancien);
        delete this.audios[ancien];
      }
      const audio = new Audio(url);
      audio.preload = "auto";
      audio.load();
      this.audios[url] = audio;
    }

    // Associe chaque fichier choisi a un "slot" via son nom (insensible a la
    // casse) : "buzz.mp3" -> buzz, "equipe_3.mp3" -> equipe n°3, etc.
    chargerFichiers(fileList) {
      const resultat = { reconnus: [], ignores: [] };
      Array.from(fileList).forEach((file) => {
        const nom = file.name.toLowerCase().replace(/\.[^.]+$/, "");
        const matchEquipe = nom.match(/^equipe[_-]?(\d+)$/);
        let cle = null;
        if (nom === "buzz") {
          cle = "buzz";
          resultat.reconnus.push(`Buzz générique ← ${file.name}`);
        } else if (nom === "victoire") {
          cle = "victoire";
          resultat.reconnus.push(`Victoire ← ${file.name}`);
        } else if (nom === "echec") {
          cle = "echec";
          resultat.reconnus.push(`Échec ← ${file.name}`);
        } else if (matchEquipe) {
          cle = `equipe_${parseInt(matchEquipe[1], 10)}`;
          resultat.reconnus.push(`Équipe ${matchEquipe[1]} ← ${file.name}`);
        } else {
          resultat.ignores.push(file.name);
        }
        if (cle) {
          this._appliquer(cle, file);
          this._sauver(cle, file);
        }
      });
      return resultat;
    }

    jouer(slot, teamIdx) {
      let url = null;
      if (slot === "buzz" && teamIdx != null && this.urls.equipes[teamIdx]) {
        url = this.urls.equipes[teamIdx];
      } else {
        url = this.urls[slot];
      }
      if (!url) return;
      try {
        const audio = this.audios[url] || new Audio(url);
        audio.currentTime = 0;
        audio.play().catch(() => {});
      } catch (e) {}
    }
  }

  // --- Gestionnaire d'État & Communication ---
  class QuizHub {
    constructor() {
      this.state = this._loadState();
      this.sons = new SoundManager();
      this.ws = null;
      this.listeners = new Set();
      this.timerInterval = null;

      // Canal local inter-onglets (pour synchroniser animateur et public sur un même PC)
      this.channel = null;
      if ("BroadcastChannel" in window) {
        this.channel = new BroadcastChannel("dmx_quiz_hub");
        this.channel.onmessage = (e) => this._onRemoteState(e.data);
      }

      this._initWebSocket();
    }

    chargerSonsPersonnalises(fileList) {
      return this.sons.chargerFichiers(fileList);
    }

    _loadState() {
      try {
        const raw = localStorage.getItem(STORAGE_KEY_STATE);
        if (raw) return Object.assign({}, defaultState, JSON.parse(raw));
      } catch (e) {}
      return JSON.parse(JSON.stringify(defaultState));
    }

    _saveState() {
      try {
        localStorage.setItem(STORAGE_KEY_STATE, JSON.stringify(this.state));
      } catch (e) {}
    }

    _notify(sourceEvent = "UPDATE", detail) {
      this._saveState();
      if (this.channel) {
        this.channel.postMessage({ type: "SYNC_STATE", state: this.state, event: sourceEvent });
      }
      this.listeners.forEach(fn => fn(this.state, sourceEvent, detail));
    }

    _onRemoteState(payload) {
      if (payload && payload.type === "SYNC_STATE") {
        this.state = payload.state;
        this.listeners.forEach(fn => fn(this.state, payload.event));
      }
    }

    subscribe(callback) {
      this.listeners.add(callback);
      callback(this.state, "INIT");
      return () => this.listeners.delete(callback);
    }

    // --- Connexion WebSocket ESP32 ---
    _initWebSocket() {
      const storedIp = localStorage.getItem(STORAGE_KEY_IP);
      const host = storedIp || (window.location.hostname && window.location.hostname !== "" ? window.location.hostname : "192.168.4.1");
      const url = `ws://${host}/ws`;

      try {
        this.ws = new WebSocket(url);
        this.ws.onopen = () => {
          this.state.connected = true;
          this._notify("WS_OPEN");
        };
        this.ws.onclose = () => {
          this.state.connected = false;
          this._notify("WS_CLOSE");
          setTimeout(() => this._initWebSocket(), 3000); // Reconnexion automatique
        };
        this.ws.onmessage = (evt) => {
          try {
            const msg = JSON.parse(evt.data);
            this._handleWsMessage(msg);
          } catch (e) {}
        };
      } catch (e) {
        this.state.connected = false;
      }
    }

    setEsp32Ip(ip) {
      localStorage.setItem(STORAGE_KEY_IP, ip.trim());
      if (this.ws) {
        this.ws.close();
      }
      this._initWebSocket();
    }

    getEsp32Ip() {
      return localStorage.getItem(STORAGE_KEY_IP) || "192.168.4.1";
    }

    _sendWs(obj) {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        this.ws.send(JSON.stringify(obj));
      }
    }

    _handleWsMessage(msg) {
      if (!msg || !msg.event) return;

      switch (msg.event) {
        case "BUZZ":
          this.triggerBuzz(msg.team - 1); // 1-index vers 0-index
          break;
        case "CORRECT":
          this.validerReponse(true);
          break;
        case "WRONG":
          this.refuserReponse(true);
          break;
        case "LOG":
          this.listeners.forEach(fn => fn(this.state, "WS_LOG", msg));
          break;
        case "SYNC_OK":
          this.listeners.forEach(fn => fn(this.state, "SYNC_OK", msg));
          break;
        case "SYNC_ERROR":
          this.listeners.forEach(fn => fn(this.state, "SYNC_ERROR", msg));
          break;
        case "MEGA_READY":
          this.listeners.forEach(fn => fn(this.state, "MEGA_READY", msg));
          break;
        case "MEGA_CONFIG":
          this._appliquerDumpMega(msg.dump);
          break;
      }
    }

    // --- Actions du Quiz ---
    triggerBuzz(teamId) {
      if (this.state.state !== "IDLE") return;
      if (this.state.bannedTeams.includes(teamId)) return;
      if (!this.state.equipes[teamId]) return;

      this.state.state = "BUZZED";
      this.state.currentTeam = teamId;
      this.state.bannedTeams.push(teamId);

      // Points direct au buzz si configuré
      if (this.state.pointsAuBuzz > 0) {
        this.state.equipes[teamId].score += this.state.pointsAuBuzz;
      }

      // Vibreur mobile
      if ("vibrate" in navigator) {
        navigator.vibrate([180, 80, 180]);
      }

      this.sons.jouer("buzz", teamId);

      // Démarrage du chrono
      this._startChrono();
      this._notify("BUZZ");
    }

    validerReponse(fromRemote = false) {
      if (this.state.state !== "BUZZED" || this.state.currentTeam === null) return;

      const tid = this.state.currentTeam;
      const pts = typeof this.state.pointsCourants === "number" ? this.state.pointsCourants : 1;
      this.state.equipes[tid].score = (this.state.equipes[tid].score || 0) + pts;

      this.sons.jouer("victoire");
      this._stopChrono();

      this.state.state = "IDLE";
      this.state.currentTeam = null;
      this.state.bannedTeams = []; // Libère toutes les équipes pour la question suivante

      if (!fromRemote) {
        this._sendWs({ type: "VALIDER" });
      }

      this._notify("CORRECT");
    }

    refuserReponse(fromRemote = false) {
      if (this.state.state !== "BUZZED") return;

      this.sons.jouer("echec");
      this._stopChrono();

      this.state.state = "IDLE";
      this.state.currentTeam = null;

      if (!fromRemote) {
        this._sendWs({ type: "REFUSER" });
      }

      this._notify("WRONG");
    }

    _startChrono() {
      this._stopChrono();
      this.state.chrono.running = true;

      if (this.state.chrono.enabled) {
        this.state.chrono.remaining = this.state.chrono.durationSec;
        this.timerInterval = setInterval(() => {
          if (this.state.chrono.remaining > 0) {
            this.state.chrono.remaining -= 1;
            this._notify("TICK");
          } else {
            this._stopChrono();
            if (this.state.chrono.timeoutFaux && this.state.state === "BUZZED") {
              this.refuserReponse();
            }
          }
        }, 1000);
      } else {
        // Chrono optionnel désactivé : chronomètre montant (temps libre sans stress)
        this.state.chrono.elapsed = 0;
        this.state.chrono.remaining = 0;
        this.timerInterval = setInterval(() => {
          this.state.chrono.elapsed = (this.state.chrono.elapsed || 0) + 1;
          this._notify("TICK_FREE");
        }, 1000);
      }
    }

    _stopChrono() {
      if (this.timerInterval) {
        clearInterval(this.timerInterval);
        this.timerInterval = null;
      }
      this.state.chrono.running = false;
    }

    toggleChrono(enabled = null) {
      if (enabled === null) {
        this.state.chrono.enabled = !this.state.chrono.enabled;
      } else {
        this.state.chrono.enabled = Boolean(enabled);
      }
      this._notify("CHRONO_CONFIG");
    }

    setChronoDuration(sec) {
      const s = Math.max(5, Math.min(300, parseInt(sec, 10) || 30));
      this.state.chrono.durationSec = s;
      if (!this.state.chrono.running) {
        this.state.chrono.remaining = s;
      }
      this._notify("CHRONO_CONFIG");
    }

    setChronoTimeoutFaux(val) {
      this.state.chrono.timeoutFaux = Boolean(val);
      this._notify("CHRONO_CONFIG");
    }

    setQuestionValue(val) {
      const parsed = parseInt(val, 10);
      this.state.pointsCourants = Math.max(0, isNaN(parsed) ? 1 : parsed);
      this._notify("CONFIG");
    }

    adjustScore(teamId, delta) {
      const eq = this.state.equipes[teamId];
      if (eq) {
        const d = parseInt(delta, 10) || 0;
        const current = typeof eq.score === "number" ? eq.score : 0;
        eq.score = Math.max(0, current + d);
        this._notify("SCORE_CHANGED");
      }
    }

    setTeamScore(teamId, score) {
      const eq = this.state.equipes[teamId];
      if (eq) {
        const parsed = parseInt(score, 10);
        eq.score = Math.max(0, isNaN(parsed) ? 0 : parsed);
        this._notify("SCORE_CHANGED");
      }
    }

    resetScores() {
      this.state.equipes.forEach(eq => {
        eq.score = 0;
      });
      this._notify("RESET_SCORES");
    }

    // --- DMX RÉGIE : 1 couleur par projecteur branché (Arduino Mega) ---
    getDmxColors(teamId) {
      const eq = this.state.equipes[teamId];
      if (!eq) return [];
      if (!Array.isArray(eq.couleurs)) eq.couleurs = [];
      while (eq.couleurs.length < this.state.projecteurs.length) {
        eq.couleurs.push(eq.couleurs[0] || "#FFFFFF");
      }
      return eq.couleurs.slice(0, this.state.projecteurs.length);
    }

    setDmxColor(teamId, projIndex, hexColor) {
      const eq = this.state.equipes[teamId];
      if (eq) {
        if (!Array.isArray(eq.couleurs)) eq.couleurs = [];
        while (eq.couleurs.length < this.state.projecteurs.length) {
          eq.couleurs.push("#FFFFFF");
        }
        eq.couleurs[projIndex] = hexColor;
        this._notify("DMX_COLOR_CHANGED");
      }
    }

    // --- ANIMATEUR : Palette visuelle de N couleurs (avec boutons + et -) ---
    getAnimatorPalette(teamId) {
      const eq = this.state.equipes[teamId];
      if (!eq) return ["#8B5CF6"];
      if (Array.isArray(eq.couleursUI) && eq.couleursUI.length > 0) {
        return eq.couleursUI;
      }
      if (Array.isArray(eq.couleurs) && eq.couleurs.length > 0) {
        return [eq.couleurs[0]];
      }
      return ["#8B5CF6"];
    }

    addAnimatorColor(teamId, hexColor = "#8B5CF6") {
      const eq = this.state.equipes[teamId];
      if (eq) {
        if (!Array.isArray(eq.couleursUI)) eq.couleursUI = [...this.getAnimatorPalette(teamId)];
        eq.couleursUI.push(hexColor);
        this._notify("ANIM_PALETTE_CHANGED");
      }
    }

    removeAnimatorColor(teamId) {
      const eq = this.state.equipes[teamId];
      if (eq && Array.isArray(eq.couleursUI) && eq.couleursUI.length > 1) {
        eq.couleursUI.pop();
        this._notify("ANIM_PALETTE_CHANGED");
      }
    }

    setAnimatorColor(teamId, colorIndex, hexColor) {
      const eq = this.state.equipes[teamId];
      if (eq) {
        if (!Array.isArray(eq.couleursUI)) eq.couleursUI = [...this.getAnimatorPalette(teamId)];
        if (eq.couleursUI[colorIndex] !== undefined) {
          eq.couleursUI[colorIndex] = hexColor;
          this._notify("ANIM_PALETTE_CHANGED");
        }
      }
    }

    // Génère le gradient CSS fluide des N couleurs choisies par l'animateur
    getTeamGradientCss(teamId) {
      const palette = this.getAnimatorPalette(teamId);
      if (!palette || palette.length === 0) return "#8B5CF6";
      if (palette.length === 1) return palette[0];
      const stops = [...palette, palette[0]];
      return `linear-gradient(135deg, ${stops.join(", ")})`;
    }

    getTeamColor(teamId) {
      const palette = this.getAnimatorPalette(teamId);
      return palette[0] || "#8B5CF6";
    }

    // Convertit un code Hexadécimal en nom de couleur lisible en français
    getColorName(hex) {
      if (!hex) return "Couleur";
      let h = String(hex).replace("#", "").trim();
      if (h.length === 3) h = h.split("").map(c => c + c).join("");
      if (h.length !== 6) return "Couleur";
      const r = parseInt(h.substring(0, 2), 16);
      const g = parseInt(h.substring(2, 4), 16);
      const b = parseInt(h.substring(4, 6), 16);
      if (isNaN(r) || isNaN(g) || isNaN(b)) return "Couleur";

      const refColors = [
        { name: "Rouge", r: 239, g: 68, b: 68 },
        { name: "Rouge", r: 255, g: 0, b: 0 },
        { name: "Bleu", r: 59, g: 130, b: 246 },
        { name: "Bleu", r: 0, g: 0, b: 255 },
        { name: "Vert", r: 16, g: 185, b: 129 },
        { name: "Vert", r: 0, g: 255, b: 0 },
        { name: "Jaune", r: 245, g: 158, b: 11 },
        { name: "Jaune", r: 255, g: 255, b: 0 },
        { name: "Orange", r: 249, g: 115, b: 22 },
        { name: "Orange", r: 255, g: 128, b: 0 },
        { name: "Rose", r: 236, g: 72, b: 153 },
        { name: "Rose", r: 255, g: 0, b: 128 },
        { name: "Violet", r: 139, g: 92, b: 246 },
        { name: "Violet", r: 168, g: 85, b: 247 },
        { name: "Cyan", r: 6, g: 182, b: 212 },
        { name: "Turquoise", r: 20, g: 184, b: 166 },
        { name: "Blanc", r: 255, g: 255, b: 255 },
        { name: "Gris", r: 156, g: 163, b: 175 },
        { name: "Noir", r: 0, g: 0, b: 0 }
      ];

      let best = "Couleur";
      let minD = Infinity;
      for (let i = 0; i < refColors.length; i++) {
        const rc = refColors[i];
        // Pondération perceptuelle humaine des composantes RVB
        const d = (r - rc.r) ** 2 * 0.3 + (g - rc.g) ** 2 * 0.59 + (b - rc.b) ** 2 * 0.11;
        if (d < minD) {
          minD = d;
          best = rc.name;
        }
      }
      return best;
    }

    // Renvoie la liste des couleurs de la palette en texte (ex: "Rouge / Bleu")
    getTeamPaletteDescription(teamId) {
      const palette = this.getAnimatorPalette(teamId);
      if (!palette || palette.length === 0) return "";
      const names = [];
      palette.forEach(hex => {
        const name = this.getColorName(hex);
        if (!names.includes(name)) names.push(name);
      });
      return names.join(" / ");
    }

    // Renvoie le libellé complet dynamique (ex: "1 Rouge / Bleu")
    getTeamFullLabel(teamId) {
      const num = teamId + 1;
      const desc = this.getTeamPaletteDescription(teamId);
      return desc ? `${num} ${desc}` : `${num}`;
    }

    setTeamName(teamId, name) {
      if (this.state.equipes[teamId]) {
        this.state.equipes[teamId].nom = name.trim() || `Équipe ${teamId + 1}`;
        this._notify("NAME_CHANGED");
      }
    }

    resetGame() {
      this.state.state = "IDLE";
      this.state.currentTeam = null;
      this.state.bannedTeams = [];
      this._stopChrono();
      this.state.equipes.forEach(eq => eq.score = 0);
      this._sendWs({ type: "RESET_ALL" });
      this._notify("RESET_GAME");
    }

    finishGame() {
      this.state.state = "FINISHED";
      this._stopChrono();
      this._notify("FINISH_GAME");
    }

    continueGame() {
      this.state.state = "IDLE";
      this.state.currentTeam = null;
      this.state.bannedTeams = [];
      this._notify("CONTINUE_GAME");
    }

    // Synchronisation de la configuration vers l'ESP32 / Mega. Le mot de
    // passe Régie vit dans config.js (pas ici) : app-common.js est partagé
    // par toutes les pages, animateur/public n'ont pas besoin de le connaître.
    // Vérifié aussi côté ESP32 (voir esp32_bridge_server.ino) — l'écran de
    // verrouillage de config.js seul ne suffirait pas contre un appel direct
    // depuis la console du navigateur.
    // Demande a la Mega sa config courante (reponse : event WS "MEGA_CONFIG").
    // La Mega est la reference commune avec le logiciel Python de config :
    // ce que l'un y ecrit (SET_* + SAVE_CONFIG), l'autre peut le relire.
    demanderConfigMega(password) {
      this._sendWs({ type: "GET_CONFIG", password: password });
    }

    // Applique un dump "CFG:..." (lignes separees par '|') sur l'etat local.
    // Ce que la Mega ne stocke pas (noms d'equipes, scores, couleurs d'interface)
    // est conserve tel quel. Retourne le resume, ou null si le dump est inutilisable.
    _appliquerDumpMega(dump) {
      const nombres = (parts) => parts.slice(2).map((v) => parseInt(v, 10));
      const hex = (r, g, b) => "#" + [r, g, b]
        .map((v) => Math.max(0, Math.min(255, v | 0)).toString(16).padStart(2, "0"))
        .join("").toUpperCase();

      let nbEq = 0;
      const projecteurs = [];
      const strobes = {};
      const couleurs = {};
      String(dump || "").split("|").forEach((ligne) => {
        const parts = ligne.split(":");
        if (parts[0] !== "CFG") return;
        const n = nombres(parts);
        if (parts[1] === "NB_EQ") {
          nbEq = n[0];
        } else if (parts[1] === "PROJ" && n.length >= 11) {
          projecteurs[n[0]] = {
            id: n[0] + 1, adresse: n[1], nbCanaux: n[2], mode: n[10],
            offDim: n[3], offR: n[4], offG: n[5], offB: n[6],
            offStrobe: n[7], strobeValue: n[8], strobeRepos: n[9]
          };
        } else if (parts[1] === "STROBE" && n.length >= 2) {
          strobes[n[0] - 1] = n[1];
        } else if (parts[1] === "COL" && n.length >= 5) {
          (couleurs[n[0] - 1] = couleurs[n[0] - 1] || {})[n[1]] = hex(n[2], n[3], n[4]);
        }
      });

      const projs = projecteurs.filter(Boolean);
      if (!(nbEq >= 1) || projs.length === 0) {
        this.listeners.forEach(fn => fn(this.state, "MEGA_CONFIG_ERROR",
          { msg: "Configuration reçue de la Mega incomplète ou vide." }));
        return null;
      }

      this.state.projecteurs = projs;
      const equipes = [];
      for (let e = 0; e < nbEq; e++) {
        const existante = this.state.equipes[e];
        const cols = projs.map((_, g) => (couleurs[e] && couleurs[e][g]) || "#FFFFFF");
        equipes.push(Object.assign(
          existante || { id: e, nom: `Équipe ${e + 1}`, score: 0, couleursUI: [cols[0], cols[0]] },
          { couleurs: cols, strobeDureeMs: strobes[e] !== undefined ? strobes[e] : 0 }
        ));
      }
      this.state.equipes = equipes;
      const resume = { nbEquipes: nbEq, nbProjecteurs: projs.length };
      this._notify("MEGA_CONFIG_APPLIED", resume);
      return resume;
    }

    syncConfigToMega(password) {
      this._sendWs({
        type: "SYNC_CONFIG",
        password: password,
        config: {
          projecteurs: this.state.projecteurs,
          equipes: this.state.equipes
        }
      });
    }
  }

  // Instance singleton partagée
  window.QuizHub = new QuizHub();

  // --- Theme clair/sombre (persistant, meme moteur d'accents Gemini) ---
  const THEME_KEY = "quiz_dmx_theme";

  function getPreferredTheme() {
    try {
      const saved = localStorage.getItem(THEME_KEY);
      if (saved === "light" || saved === "dark") return saved;
    } catch (e) {}
    try {
      if (window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches) return "light";
    } catch (e) {}
    return "dark";
  }

  function applyTheme(theme) {
    document.documentElement.setAttribute("data-theme", theme);
    try {
      const meta = document.querySelector('meta[name="theme-color"]');
      if (meta) meta.setAttribute("content", theme === "light" ? "#F1F4F9" : "#080B11");
    } catch (e) {}
    document.querySelectorAll(".theme-toggle-btn").forEach((btn) => {
      const label = theme === "light" ? "Passer en mode sombre" : "Passer en mode clair";
      btn.textContent = theme === "light" ? "🌙" : "☀️";
      btn.setAttribute("aria-label", label);
      btn.title = label;
    });
  }

  function setTheme(theme) {
    try { localStorage.setItem(THEME_KEY, theme); } catch (e) {}
    applyTheme(theme);
  }

  function toggleTheme() {
    const current = document.documentElement.getAttribute("data-theme") || "dark";
    setTheme(current === "light" ? "dark" : "light");
  }

  function injectThemeToggle() {
    const header = document.querySelector(".header-meta");
    if (!header || header.querySelector(".theme-toggle-btn")) return;
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "btn btn-subtle theme-toggle-btn";
    btn.addEventListener("click", toggleTheme);
    header.insertBefore(btn, header.firstChild);
    applyTheme(document.documentElement.getAttribute("data-theme") || getPreferredTheme());
  }

  // Applique tout de suite (evite un flash si la page n'a pas deja pose
  // data-theme via le petit script inline du <head>), puis injecte le bouton.
  applyTheme(document.documentElement.getAttribute("data-theme") || getPreferredTheme());
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", injectThemeToggle);
  } else {
    injectThemeToggle();
  }

  window.QuizHub.toggleTheme = toggleTheme;

  // --- Modales maison (remplace alert()/confirm()/prompt() natifs, moches
  // et non themes, par des popups dans le style de l'appli) ---
  function escapeHtml(str) {
    const div = document.createElement("div");
    div.textContent = str == null ? "" : String(str);
    return div.innerHTML;
  }

  function buildModal(innerHtml) {
    const backdrop = document.createElement("div");
    backdrop.className = "modal-backdrop";
    backdrop.innerHTML = `<div class="modal-content">${innerHtml}</div>`;
    document.body.appendChild(backdrop);
    return backdrop;
  }

  function destroyModal(backdrop) {
    if (backdrop && backdrop.parentNode) backdrop.parentNode.removeChild(backdrop);
  }

  function uiAlert(message, opts) {
    opts = opts || {};
    return new Promise((resolve) => {
      const backdrop = buildModal(`
        <p style="margin-bottom:20px; white-space: pre-line;">${escapeHtml(message)}</p>
        <div style="display:flex; justify-content:flex-end;">
          <button type="button" class="btn btn-primary" data-modal-ok>${escapeHtml(opts.okLabel || "OK")}</button>
        </div>
      `);
      const finish = () => { destroyModal(backdrop); resolve(); };
      backdrop.querySelector("[data-modal-ok]").addEventListener("click", finish);
      backdrop.addEventListener("click", (e) => { if (e.target === backdrop) finish(); });
    });
  }

  function uiConfirm(message, opts) {
    opts = opts || {};
    return new Promise((resolve) => {
      const backdrop = buildModal(`
        <p style="margin-bottom:20px; white-space: pre-line;">${escapeHtml(message)}</p>
        <div style="display:flex; justify-content:flex-end; gap:10px;">
          <button type="button" class="btn btn-subtle" data-modal-cancel>${escapeHtml(opts.cancelLabel || "Annuler")}</button>
          <button type="button" class="btn ${opts.danger ? "btn-danger" : "btn-primary"}" data-modal-ok>${escapeHtml(opts.okLabel || "Confirmer")}</button>
        </div>
      `);
      const finish = (val) => { destroyModal(backdrop); resolve(val); };
      backdrop.querySelector("[data-modal-ok]").addEventListener("click", () => finish(true));
      backdrop.querySelector("[data-modal-cancel]").addEventListener("click", () => finish(false));
      backdrop.addEventListener("click", (e) => { if (e.target === backdrop) finish(false); });
    });
  }

  function uiPrompt(message, defaultValue, opts) {
    opts = opts || {};
    return new Promise((resolve) => {
      const inputId = "modal-prompt-input-" + Math.random().toString(36).slice(2);
      const backdrop = buildModal(`
        <p style="margin-bottom:12px; white-space: pre-line;">${escapeHtml(message)}</p>
        <input id="${inputId}" type="${opts.type === "number" ? "number" : "text"}" class="modal-prompt-input"
               value="${escapeHtml(defaultValue == null ? "" : defaultValue)}">
        <div style="display:flex; justify-content:flex-end; gap:10px; margin-top:16px;">
          <button type="button" class="btn btn-subtle" data-modal-cancel>Annuler</button>
          <button type="button" class="btn btn-primary" data-modal-ok>OK</button>
        </div>
      `);
      const input = backdrop.querySelector("#" + inputId);
      const finish = (val) => { destroyModal(backdrop); resolve(val); };
      backdrop.querySelector("[data-modal-ok]").addEventListener("click", () => finish(input.value));
      backdrop.querySelector("[data-modal-cancel]").addEventListener("click", () => finish(null));
      backdrop.addEventListener("click", (e) => { if (e.target === backdrop) finish(null); });
      input.addEventListener("keydown", (e) => {
        if (e.key === "Enter") finish(input.value);
        if (e.key === "Escape") finish(null);
      });
      setTimeout(() => { input.focus(); input.select(); }, 30);
    });
  }

  /** Liste de choix tactile (remplace un prompt() numerique par des boutons a taper). */
  function uiChoose(message, options) {
    return new Promise((resolve) => {
      const buttonsHtml = options.map((opt, i) =>
        `<button type="button" class="btn btn-subtle" data-modal-choice="${i}" style="width:100%; justify-content:flex-start; margin-bottom:8px;">${escapeHtml(opt.label)}</button>`
      ).join("");
      const backdrop = buildModal(`
        <p style="margin-bottom:14px; white-space: pre-line;">${escapeHtml(message)}</p>
        <div style="max-height:50vh; overflow-y:auto;">${buttonsHtml}</div>
        <div style="display:flex; justify-content:flex-end; margin-top:10px;">
          <button type="button" class="btn btn-subtle" data-modal-cancel>Annuler</button>
        </div>
      `);
      const finish = (val) => { destroyModal(backdrop); resolve(val); };
      backdrop.querySelectorAll("[data-modal-choice]").forEach((btn) => {
        btn.addEventListener("click", () => finish(options[parseInt(btn.dataset.modalChoice, 10)].value));
      });
      backdrop.querySelector("[data-modal-cancel]").addEventListener("click", () => finish(null));
      backdrop.addEventListener("click", (e) => { if (e.target === backdrop) finish(null); });
    });
  }

  window.QuizHub.uiAlert = uiAlert;
  window.QuizHub.uiConfirm = uiConfirm;
  window.QuizHub.uiPrompt = uiPrompt;
  window.QuizHub.uiChoose = uiChoose;

})(window);
