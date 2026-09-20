/**
 * ==========================================================================
 * CONFIG.JS - Logique Régie DMX (Colonnes, Palettes N Couleurs & Chrono)
 * ==========================================================================
 */

(function () {
  "use strict";

  const hub = window.QuizHub;

  // Mot de passe Régie — DOIT être identique à CONFIG_PASSWORD dans
  // esp32/esp32_bridge_server.ino (changez les DEUX si vous le modifiez).
  // Écran de verrouillage = dissuasion visuelle ; la vraie protection contre
  // un appel direct (console navigateur) est la vérification côté ESP32.
  const CONFIG_PASSWORD = "regie2026";
  const LOCK_SESSION_KEY = "quiz_dmx_config_unlocked";

  (function initLockScreen() {
    const lockScreen = document.getElementById("config-lock-screen");
    const lockInput = document.getElementById("config-lock-input");
    const lockError = document.getElementById("config-lock-error");
    const lockSubmit = document.getElementById("config-lock-submit");
    if (!lockScreen) return;

    let dejaDeverrouille = false;
    try { dejaDeverrouille = sessionStorage.getItem(LOCK_SESSION_KEY) === "1"; } catch (e) {}
    if (dejaDeverrouille) {
      lockScreen.style.display = "none";
      return;
    }

    function tryUnlock() {
      if (lockInput.value === CONFIG_PASSWORD) {
        try { sessionStorage.setItem(LOCK_SESSION_KEY, "1"); } catch (e) {}
        lockScreen.style.display = "none";
      } else {
        lockError.textContent = "Mot de passe incorrect.";
        lockInput.value = "";
        lockInput.focus();
      }
    }

    lockSubmit.addEventListener("click", tryUnlock);
    lockInput.addEventListener("keydown", (e) => { if (e.key === "Enter") tryUnlock(); });
    setTimeout(() => lockInput.focus(), 50);
  })();

  // DOM Elements
  const connDot = document.getElementById("conn-dot");
  const fixturesColumnList = document.getElementById("fixtures-column-list");
  const badgeFixturesCount = document.getElementById("badge-fixtures-count");
  const btnAddFixture = document.getElementById("btn-add-fixture");

  const badgeEquipesCount = document.getElementById("badge-equipes-count");
  const teamsPalettesContainer = document.getElementById("teams-palettes-container");
  const btnAddTeam = document.getElementById("btn-add-team");
  const btnRemoveTeam = document.getElementById("btn-remove-team");

  // Chrono controls
  const chkChronoActive = document.getElementById("chk-chrono-active");
  const badgeChronoMode = document.getElementById("badge-chrono-mode");
  const txtChronoToggleLabel = document.getElementById("txt-chrono-toggle-label");
  const grpChronoDuration = document.getElementById("grp-chrono-duration");
  const inputChronoSec = document.getElementById("input-chrono-sec");
  const chkTimeoutFaux = document.getElementById("chk-timeout-faux");

  // Sync & Logs
  const btnSyncMega = document.getElementById("btn-sync-mega");
  const btnReadMega = document.getElementById("btn-read-mega");
  const logsConsole = document.getElementById("logs-console");
  const btnClearLogs = document.getElementById("btn-clear-logs");

  // Export / Import
  const btnExportJson = document.getElementById("btn-export-json");
  const btnImportJson = document.getElementById("btn-import-json");
  const inputImportFile = document.getElementById("input-import-file");
  const modalExport = document.getElementById("modal-export");
  const textareaExport = document.getElementById("textarea-export");
  const btnCloseExportModal = document.getElementById("btn-close-export-modal");
  const btnCopyExport = document.getElementById("btn-copy-export");

  function addLog(msg, color = "#06B6D4") {
    const time = new Date().toLocaleTimeString();
    const line = document.createElement("div");
    line.style.color = color;
    line.textContent = `[${time}] ${msg}`;
    logsConsole.appendChild(line);
    logsConsole.scrollTop = logsConsole.scrollHeight;
  }

  // --- Rendu complet de la configuration ---
  function render(state) {
    if (state.connected) {
      connDot.className = "status-dot online";
    } else {
      connDot.className = "status-dot";
    }

    // 1. État du chrono optionnel
    const isChronoOn = state.chrono.enabled;
    chkChronoActive.checked = isChronoOn;
    badgeChronoMode.textContent = isChronoOn ? `${state.chrono.durationSec}s` : "Désactivé (Temps Libre)";
    badgeChronoMode.style.color = isChronoOn ? "var(--color-success)" : "var(--text-muted)";
    txtChronoToggleLabel.textContent = isChronoOn ? `Chronomètre Actif (${state.chrono.durationSec}s)` : "Chronomètre Désactivé";
    grpChronoDuration.style.opacity = isChronoOn ? "1" : "0.4";
    inputChronoSec.value = state.chrono.durationSec;
    chkTimeoutFaux.checked = state.chrono.timeoutFaux;

    // 2. Projecteurs en COLONNE
    renderFixturesColumn(state);

    // 3. Équipes avec Palettes de N Couleurs
    renderTeamsPalettes(state);
  }

  // Rendu des projecteurs en COLONNE verticale empilée
  function renderFixturesColumn(state) {
    badgeFixturesCount.textContent = `${state.projecteurs.length} projecteur${state.projecteurs.length > 1 ? 's' : ''}`;
    fixturesColumnList.innerHTML = "";

    state.projecteurs.forEach((fix, idx) => {
      const card = document.createElement("div");
      card.className = "fixture-card";

      card.innerHTML = `
        <div class="fixture-card-header">
          <div style="display: flex; align-items: center; gap: 10px;">
            <div style="font-weight: 800; font-size: 1.15rem; color: var(--gemini-purple);">
              Projecteur ${idx + 1}
            </div>
            <span class="badge mono" style="font-size: 0.75rem;">Canal ${fix.adresse} ➔ ${fix.adresse + fix.nbCanaux - 1}</span>
            <span class="badge" style="font-size: 0.75rem; background: rgba(139, 92, 246, 0.1); color: var(--gemini-cyan);">
              ${fix.mode === 1 ? 'Roue Lyre' : 'RGB Continu'}
            </span>
          </div>

          <button class="btn btn-subtle" data-del-fix="${idx}" style="color: var(--color-danger); padding: 4px 10px; font-size: 0.8rem;">
            ✕ Supprimer
          </button>
        </div>

        <div style="display: flex; flex-direction: column; gap: 12px;">
          
          <div class="field-group">
            <label>Adresse DMX (1-512)</label>
            <input type="number" min="1" max="512" value="${fix.adresse}" data-idx="${idx}" data-field="adresse">
          </div>

          <div class="field-group">
            <label>Nombre de Canaux</label>
            <input type="number" min="1" max="32" value="${fix.nbCanaux}" data-idx="${idx}" data-field="nbCanaux">
          </div>

          <div class="field-group">
            <label>Mode Éclairage</label>
            <select data-idx="${idx}" data-field="mode">
              <option value="0" ${fix.mode === 0 ? 'selected' : ''}>RGB continu (Par LED)</option>
              <option value="1" ${fix.mode === 1 ? 'selected' : ''}>Roue de couleurs (Lyre)</option>
            </select>
          </div>

          <div class="field-group">
            <label>Offset Dimmer</label>
            <input type="number" value="${fix.offDim}" data-idx="${idx}" data-field="offDim">
          </div>

          <div class="field-group">
            <label>${fix.mode === 1 ? 'Offset Canal Roue' : 'Offset Rouge'}</label>
            <input type="number" value="${fix.offR}" data-idx="${idx}" data-field="offR">
          </div>

          <div class="field-group" style="${fix.mode === 1 ? 'opacity: 0.35;' : ''}">
            <label>Offset Vert</label>
            <input type="number" value="${fix.offG}" data-idx="${idx}" data-field="offG" ${fix.mode === 1 ? 'disabled' : ''}>
          </div>

          <div class="field-group" style="${fix.mode === 1 ? 'opacity: 0.35;' : ''}">
            <label>Offset Bleu</label>
            <input type="number" value="${fix.offB}" data-idx="${idx}" data-field="offB" ${fix.mode === 1 ? 'disabled' : ''}>
          </div>

          <div class="field-group">
            <label>Offset Strobe (-1 = aucun)</label>
            <input type="number" value="${fix.offStrobe}" data-idx="${idx}" data-field="offStrobe">
          </div>

          <div class="field-group">
            <label>Valeur Strobe (0-255)</label>
            <input type="number" value="${fix.strobeValue}" data-idx="${idx}" data-field="strobeValue">
          </div>

          <div class="field-group">
            <label>Valeur Repos Obturateur</label>
            <input type="number" value="${fix.strobeRepos}" data-idx="${idx}" data-field="strobeRepos">
          </div>

        </div>
      `;

      fixturesColumnList.appendChild(card);
    });

    // Écouteurs sur les champs de projecteurs
    fixturesColumnList.querySelectorAll("input, select").forEach(elem => {
      elem.onchange = () => {
        const idx = parseInt(elem.dataset.idx, 10);
        const field = elem.dataset.field;
        const val = parseInt(elem.value, 10);
        if (hub.state.projecteurs[idx]) {
          hub.state.projecteurs[idx][field] = val;
          hub._notify("CONFIG");
        }
      };
    });

    // Suppression de projecteur
    fixturesColumnList.querySelectorAll("[data-del-fix]").forEach(btn => {
      btn.onclick = () => {
        const idx = parseInt(btn.dataset.delFix, 10);
        if (hub.state.projecteurs.length <= 1) {
          hub.uiAlert("Vous devez conserver au moins un projecteur.");
          return;
        }
        hub.uiConfirm(`Supprimer le Projecteur ${idx + 1} ?`, { danger: true }).then((ok) => {
          if (!ok) return;
          hub.state.projecteurs.splice(idx, 1);
          hub._notify("CONFIG");
        });
      };
    });
  }

  // Valeurs DMX indicatives d'une roue de couleurs "standard" 8 teintes —
  // n'est PAS universel : chaque marque/modele de lyre a sa propre roue
  // (valeurs et ordre differents). Sert uniquement de point de depart rapide
  // dans le champ manuel ci-dessous ; a verifier/ajuster avec le manuel du
  // projecteur reellement utilise.
  const ROUE_PRESETS = [
    { label: "Blanc", valeur: 4 },
    { label: "Rouge", valeur: 14 },
    { label: "Vert", valeur: 24 },
    { label: "Bleu", valeur: 34 },
    { label: "Jaune", valeur: 44 },
    { label: "Rose", valeur: 54 },
    { label: "Orange", valeur: 64 },
    { label: "Cyan", valeur: 74 }
  ];

  // La valeur de roue est stockee dans l'octet Rouge du hex couleur
  // (#XX0000, G/B inutilises pour ce canal) — meme format de stockage/
  // transmission SET_COL que le mode RVB, juste reinterprete cote Mega
  // (voir setProjecteur() dans megaf.ino).
  function hexVersValeurRoue(hex) {
    const n = parseInt((hex || "#000000").slice(1, 3), 16);
    return Number.isFinite(n) ? n : 0;
  }
  function valeurRoueVersHex(valeur) {
    const v = Math.max(0, Math.min(255, parseInt(valeur, 10) || 0));
    return "#" + v.toString(16).padStart(2, "0").toUpperCase() + "0000";
  }

  // Rendu des Équipes avec 1 couleur DMX par projecteur physique
  function renderTeamsPalettes(state) {
    badgeEquipesCount.textContent = `${state.equipes.length} équipes`;
    teamsPalettesContainer.innerHTML = "";

    const nbProj = state.projecteurs.length;

    state.equipes.forEach((eq, eqIdx) => {
      const card = document.createElement("div");
      card.className = "team-palette-card";

      // Couleurs DMX matérielles (1 par projecteur)
      const dmxColors = hub.getDmxColors(eqIdx);

      // Génère les pastilles de couleur (RVB) ou le champ de valeur de roue
      // (mode Lyre) pour chaque projecteur branché
      let swatchesHtml = "";
      state.projecteurs.forEach((proj, projIdx) => {
        const color = dmxColors[projIdx] || "#FFFFFF";

        if (proj.mode === 1) {
          const valeur = hexVersValeurRoue(color);
          const presetsHtml = ROUE_PRESETS.map(p =>
            `<button type="button" class="btn-palette-adjust" data-roue-preset="${p.valeur}" data-team-id="${eqIdx}" data-proj-idx="${projIdx}"
                     title="${p.label} (valeur indicative ${p.valeur})" style="width: 22px; height: 22px; font-size: 0.6rem; padding: 0;">${p.label[0]}</button>`
          ).join("");
          swatchesHtml += `
            <div style="display: flex; flex-direction: column; align-items: center; gap: 4px;">
              <span style="font-size: 0.75rem; color: var(--text-muted); font-weight: 700;">Proj ${projIdx + 1} (roue)</span>
              <input type="number" min="0" max="255" value="${valeur}" data-team-id="${eqIdx}" data-proj-idx="${projIdx}" data-roue-value="1"
                     title="Valeur DMX brute de la roue (0-255) — voir le manuel du projecteur"
                     style="width: 60px; background: var(--bg-input); border: 1px solid var(--border-subtle); border-radius: 6px; color: #fff; padding: 4px 6px; font-size: 0.85rem; text-align: center;">
              <div style="display: flex; gap: 2px; flex-wrap: wrap; max-width: 90px; justify-content: center;">${presetsHtml}</div>
            </div>
          `;
        } else {
          swatchesHtml += `
            <div style="display: flex; flex-direction: column; align-items: center; gap: 4px;">
              <span style="font-size: 0.75rem; color: var(--text-muted); font-weight: 700;">Proj ${projIdx + 1}</span>
              <div class="palette-swatch-box" style="background-color: ${color};" title="Projecteur ${projIdx + 1} (Canal ${proj.adresse})">
                <input type="color" value="${color}" data-team-id="${eqIdx}" data-proj-idx="${projIdx}">
              </div>
            </div>
          `;
        }
      });

      card.innerHTML = `
        <div class="team-palette-header">
          <div style="display: flex; align-items: center; gap: 12px;">
            <input type="text" value="${eq.nom}" data-team-name="${eqIdx}" style="background: transparent; border: 1px solid transparent; border-bottom: 1px solid var(--border-subtle); color: #fff; font-weight: 800; font-size: 1.1rem; width: 140px; padding: 4px;">
            <span class="badge mono" style="font-size: 0.75rem;">${nbProj} projecteur${nbProj > 1 ? 's' : ''}</span>
          </div>

          <div style="display: flex; align-items: center; gap: 12px;">
            <div style="display: flex; align-items: center; gap: 6px;">
              <span style="font-size: 0.8rem; color: var(--text-secondary);">Strobe :</span>
              <input type="number" value="${eq.strobeDureeMs ?? 0}" data-team-strobe="${eqIdx}" style="width: 75px; background: var(--bg-input); border: 1px solid var(--border-subtle); border-radius: 6px; color: #fff; padding: 4px 6px; font-size: 0.85rem;" title="Durée strobe ms (0 = aucun, -1 = continu)">
              <span style="font-size: 0.75rem; color: var(--text-muted);">ms</span>
            </div>
            <button class="btn btn-subtle" data-del-team="${eqIdx}" style="color: var(--color-danger); padding: 4px 8px; font-size: 0.8rem;">
              ✕
            </button>
          </div>
        </div>

        <!-- Rangée des couleurs DMX par projecteur (1 couleur par projecteur branché) -->
        <div style="display: flex; align-items: center; justify-content: space-between; gap: 14px; flex-wrap: wrap; margin-top: 4px;">
          <div style="display: flex; align-items: center; gap: 12px; flex-wrap: wrap;">
            <span style="font-size: 0.85rem; color: var(--text-secondary); font-weight: 600;">Couleurs DMX :</span>
            <div class="palette-swatches-row">
              ${swatchesHtml}
            </div>
          </div>

          <div style="font-size: 0.75rem; color: var(--text-muted);">
            Envoyé à l'Arduino Mega (SET_COL)
          </div>
        </div>
      `;

      teamsPalettesContainer.appendChild(card);
    });

    // Écouteurs sur les sélecteurs de couleur DMX (mode RVB continu)
    teamsPalettesContainer.querySelectorAll("input[type='color']").forEach(input => {
      input.onchange = (e) => {
        const tId = parseInt(input.dataset.teamId, 10);
        const pIdx = parseInt(input.dataset.projIdx, 10);
        hub.setDmxColor(tId, pIdx, e.target.value);
        addLog(`Équipe ${tId + 1} / Projecteur ${pIdx + 1} : couleur DMX mise à jour (${e.target.value})`, "var(--gemini-cyan)");
      };
    });

    // Écouteurs sur le champ de valeur de roue (mode Lyre) et ses préréglages
    teamsPalettesContainer.querySelectorAll("input[data-roue-value]").forEach(input => {
      input.onchange = (e) => {
        const tId = parseInt(input.dataset.teamId, 10);
        const pIdx = parseInt(input.dataset.projIdx, 10);
        const valeur = Math.max(0, Math.min(255, parseInt(e.target.value, 10) || 0));
        hub.setDmxColor(tId, pIdx, valeurRoueVersHex(valeur));
        addLog(`Équipe ${tId + 1} / Projecteur ${pIdx + 1} : valeur de roue mise à jour (${valeur})`, "var(--gemini-cyan)");
      };
    });
    teamsPalettesContainer.querySelectorAll("[data-roue-preset]").forEach(btn => {
      btn.onclick = (e) => {
        e.stopPropagation();
        const tId = parseInt(btn.dataset.teamId, 10);
        const pIdx = parseInt(btn.dataset.projIdx, 10);
        const valeur = parseInt(btn.dataset.rouePreset, 10);
        hub.setDmxColor(tId, pIdx, valeurRoueVersHex(valeur));
        addLog(`Équipe ${tId + 1} / Projecteur ${pIdx + 1} : préréglage roue appliqué (${valeur}) — à vérifier avec votre projecteur`, "var(--gemini-cyan)");
      };
    });

    // Écouteur sur le renommage d'équipe
    teamsPalettesContainer.querySelectorAll("[data-team-name]").forEach(input => {
      input.onchange = (e) => {
        const tId = parseInt(input.dataset.teamName, 10);
        hub.setTeamName(tId, e.target.value);
      };
    });

    // Écouteur sur le strobe
    teamsPalettesContainer.querySelectorAll("[data-team-strobe]").forEach(input => {
      input.onchange = (e) => {
        const tId = parseInt(input.dataset.teamStrobe, 10);
        if (hub.state.equipes[tId]) {
          hub.state.equipes[tId].strobeDureeMs = parseInt(e.target.value, 10) || 0;
          hub._notify("CONFIG");
        }
      };
    });

    // Suppression d'équipe spécifique
    teamsPalettesContainer.querySelectorAll("[data-del-team]").forEach(btn => {
      btn.onclick = () => {
        const tId = parseInt(btn.dataset.delTeam, 10);
        if (hub.state.equipes.length <= 1) {
          hub.uiAlert("Il faut au minimum 1 équipe.");
          return;
        }
        hub.uiConfirm(`Supprimer l'Équipe ${hub.state.equipes[tId].nom} ?`, { danger: true }).then((ok) => {
          if (!ok) return;
          hub.state.equipes.splice(tId, 1);
          hub._notify("CONFIG");
        });
      };
    });
  }

  // --- Gestion du Chrono Optionnel & Réglable ---
  chkChronoActive.onchange = (e) => {
    hub.toggleChrono(e.target.checked);
    addLog(`Chronomètre ${e.target.checked ? 'activé' : 'désactivé (temps libre)'}`, e.target.checked ? "var(--color-success)" : "var(--color-warning)");
  };

  inputChronoSec.onchange = (e) => {
    hub.setChronoDuration(e.target.value);
    addLog(`Durée du chronomètre définie à ${e.target.value}s`, "var(--gemini-cyan)");
  };

  document.querySelectorAll("[data-preset]").forEach(btn => {
    btn.onclick = () => {
      const sec = parseInt(btn.dataset.preset, 10);
      hub.setChronoDuration(sec);
      addLog(`Preset chrono sélectionné : ${sec} secondes`, "var(--gemini-cyan)");
    };
  });

  chkTimeoutFaux.onchange = (e) => {
    hub.setChronoTimeoutFaux(e.target.checked);
  };

  // Ajouter un projecteur
  btnAddFixture.onclick = () => {
    if (hub.state.projecteurs.length >= 30) {
      hub.uiAlert("Limite de 30 projecteurs atteinte.");
      return;
    }
    const last = hub.state.projecteurs[hub.state.projecteurs.length - 1];
    const newAddress = last ? (last.adresse + last.nbCanaux) : 1;
    const newId = hub.state.projecteurs.length + 1;

    hub.state.projecteurs.push({
      id: newId,
      adresse: Math.min(512, newAddress),
      nbCanaux: 8,
      mode: 0,
      offDim: 0,
      offR: 1,
      offG: 2,
      offB: 3,
      offStrobe: -1,
      strobeValue: 200,
      strobeRepos: 0
    });

    addLog(`Projecteur ${newId} ajouté (Adresse ${newAddress})`, "var(--gemini-purple)");
    hub._notify("CONFIG");
  };

  // Ajouter une équipe
  btnAddTeam.onclick = () => {
    if (hub.state.equipes.length >= 30) {
      hub.uiAlert("Limite de 30 équipes atteinte.");
      return;
    }
    const newId = hub.state.equipes.length;
    const defaultColor = ["#EF4444", "#3B82F6", "#10B981", "#F59E0B", "#EC4899", "#06B6D4"][newId % 6];
    
    hub.state.equipes.push({
      id: newId,
      nom: `Équipe ${newId + 1}`,
      score: 0,
      couleursUI: [defaultColor, "#8B5CF6"],
      couleurs: [defaultColor],
      strobeDureeMs: 0
    });

    addLog(`Équipe ${newId + 1} ajoutée`, "var(--color-success)");
    hub._notify("CONFIG");
  };

  // Supprimer la dernière équipe
  btnRemoveTeam.onclick = () => {
    if (hub.state.equipes.length <= 1) {
      hub.uiAlert("Il faut au minimum 1 équipe.");
      return;
    }
    const removed = hub.state.equipes.pop();
    addLog(`Équipe ${removed.nom} supprimée`, "var(--color-warning)");
    hub._notify("CONFIG");
  };

  // Synchronisation vers l'ESP32 et la Mega
  // [FIX] Auparavant, un setTimeout(1200ms) affichait de faux logs "SET_PATCH
  // envoyé", "SAVE_CONFIG : Écrit dans l'EEPROM" puis "Synchronisé avec succès"
  // SANS AUCUN LIEN avec la vraie config -- meme Mega/ESP32 eteints, le
  // technicien voyait "succès". Le vrai retour existe deja (Mega -> ESP32
  // parseLigneMegaConf() -> WS event "LOG" avec CONF:.../ERR:...), il suffit
  // de l'ecouter (voir hub.subscribe plus bas) au lieu de la simuler.
  let syncEnCours = false;
  let syncTimeoutId = null;

  function terminerSync(succes, detail) {
    syncEnCours = false;
    if (syncTimeoutId) { clearTimeout(syncTimeoutId); syncTimeoutId = null; }
    btnSyncMega.disabled = false;
    btnSyncMega.textContent = "⚡ Synchroniser la Mega";
    if (succes) {
      hub.uiAlert("Configuration synchronisée avec succès avec l'Arduino Mega !");
    } else {
      hub.uiAlert(detail || "Échec de la synchronisation : vérifier que l'ESP32 et la Mega sont bien connectés et alimentés.");
    }
  }

  btnSyncMega.onclick = () => {
    if (syncEnCours) return;
    if (!hub.state.connected) {
      hub.uiAlert("ESP32 non connecté (Wi-Fi) : impossible de synchroniser la Mega.");
      return;
    }
    syncEnCours = true;
    btnSyncMega.disabled = true;
    btnSyncMega.textContent = "⏳ Synchronisation en cours...";
    addLog("Envoi de la configuration vers l'ESP32 & Arduino Mega...", "var(--color-gold)");

    hub.syncConfigToMega(CONFIG_PASSWORD);

    syncTimeoutId = setTimeout(() => {
      terminerSync(false, "Aucune confirmation reçue de la Mega après 6s (liaison ESP32↔Mega EEPROM à vérifier).");
    }, 6000);
  };

  // Lecture de la config depuis la Mega (source commune avec le logiciel Python).
  // La Mega ne connait pas les noms d'equipes ni les scores : ils sont conserves.
  let lectureEnCours = false;
  let lectureTimeoutId = null;

  function terminerLecture(succes, detail) {
    lectureEnCours = false;
    if (lectureTimeoutId) { clearTimeout(lectureTimeoutId); lectureTimeoutId = null; }
    btnReadMega.disabled = false;
    btnReadMega.textContent = "⬇ Lire depuis la Mega";
    if (succes) {
      hub.uiAlert(detail || "Configuration lue depuis la Mega.");
    } else {
      hub.uiAlert(detail || "Échec de la lecture : vérifier que l'ESP32 et la Mega sont connectés et alimentés.");
    }
  }

  btnReadMega.onclick = () => {
    if (lectureEnCours || syncEnCours) return;
    if (!hub.state.connected) {
      hub.uiAlert("ESP32 non connecté (Wi-Fi) : impossible de lire la Mega.");
      return;
    }
    hub.uiConfirm(
      "Remplacer les projecteurs, couleurs et strobes affichés ici par ceux enregistrés dans la Mega ?\n" +
      "(Les noms d'équipes et les scores sont conservés.)"
    ).then((ok) => {
      if (!ok) return;
      lectureEnCours = true;
      btnReadMega.disabled = true;
      btnReadMega.textContent = "⏳ Lecture en cours...";
      addLog("Lecture de la configuration depuis la Mega...", "var(--color-gold)");
      hub.demanderConfigMega(CONFIG_PASSWORD);
      lectureTimeoutId = setTimeout(() => {
        terminerLecture(false, "Aucune réponse de la Mega après 15s (liaison ESP32↔Mega à vérifier, ou Mega sans le firmware GET_CONFIG).");
      }, 15000);
    });
  };

  // Export JSON
  btnExportJson.onclick = () => {
    const data = {
      chrono: hub.state.chrono,
      projecteurs: hub.state.projecteurs,
      equipes: hub.state.equipes
    };
    textareaExport.value = JSON.stringify(data, null, 2);
    modalExport.style.display = "flex";
  };

  btnCloseExportModal.onclick = () => {
    modalExport.style.display = "none";
  };

  btnCopyExport.onclick = () => {
    textareaExport.select();
    navigator.clipboard.writeText(textareaExport.value).then(() => {
      hub.uiAlert("Configuration copiée dans le presse-papier !");
    });
  };

  // Import JSON
  btnImportJson.onclick = () => inputImportFile.click();

  inputImportFile.onchange = (e) => {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (evt) => {
      try {
        const parsed = JSON.parse(evt.target.result);
        if (parsed.projecteurs && parsed.equipes) {
          hub.state.projecteurs = parsed.projecteurs;
          hub.state.equipes = parsed.equipes;
          if (parsed.chrono) hub.state.chrono = parsed.chrono;
          hub._notify("CONFIG_IMPORTED");
          addLog("Configuration JSON chargée avec succès", "var(--color-success)");
        } else {
          hub.uiAlert("Fichier JSON non conforme.");
        }
      } catch (err) {
        hub.uiAlert("Erreur de lecture du JSON.");
      }
    };
    reader.readAsText(file);
  };

  btnClearLogs.onclick = () => {
    logsConsole.innerHTML = "";
  };

  // Souscription aux mises à jour
  hub.subscribe(render);

  // Vrais retours Mega/ESP32 (voir commentaire [FIX] sur btnSyncMega plus haut) :
  // esp32_bridge_server.ino relaie chaque ligne CONF:.../ERR:... de la Mega
  // via un event WS "LOG" (parseLigneMegaConf -> broadcastLog).
  hub.subscribe((state, eventType, msg) => {
    if (eventType === "MEGA_CONFIG_APPLIED" && lectureEnCours) {
      const r = msg || {};
      terminerLecture(true, `Configuration lue depuis la Mega : ${r.nbProjecteurs} projecteur(s), ${r.nbEquipes} équipe(s).`);
      return;
    }
    if (eventType === "MEGA_CONFIG_ERROR" && lectureEnCours) {
      terminerLecture(false, (msg && msg.msg) || undefined);
      return;
    }
    if (eventType !== "WS_LOG" || !msg) return;
    const texte = String(msg.msg || "");
    addLog(texte, msg.color || "var(--text-secondary)");
    if (lectureEnCours && texte.includes("GET_CONFIG_BUSY")) {
      terminerLecture(false, "La Mega est en manche (buzz en cours) : valider ou refuser d'abord, puis relire.");
      return;
    }
    if (!syncEnCours) return;
    if (texte.includes("SAVED_TO_EEPROM")) {
      terminerSync(true);
    } else if (texte.includes("ERR:")) {
      terminerSync(false, `Erreur renvoyée par la Mega : ${texte}`);
    }
  });

})();
