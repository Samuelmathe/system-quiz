/**
 * ==========================================================================
 * ANIMATEUR.JS - Logique mobile de la télécommande du Quiz
 * ==========================================================================
 */

(function () {
  "use strict";

  const hub = window.QuizHub;

  // Éléments du DOM
  const buzzHero = document.getElementById("buzz-hero");
  const buzzStatusText = document.getElementById("buzz-status-text");
  const buzzTeamName = document.getElementById("buzz-team-name");
  const buzzTimerDisplay = document.getElementById("buzz-timer-display");
  const buzzActionRow = document.getElementById("buzz-action-row");
  const btnValider = document.getElementById("btn-valider");
  const btnValiderSub = document.getElementById("btn-valider-sub");
  const btnRefuser = document.getElementById("btn-refuser");

  const connDot = document.getElementById("conn-dot");
  const badgeStatus = document.getElementById("badge-status");

  const displayPtsValeur = document.getElementById("display-pts-valeur");
  const btnPtsMinus = document.getElementById("btn-pts-minus");
  const btnPtsPlus = document.getElementById("btn-pts-plus");

  const teamsGrid = document.getElementById("teams-grid-container");
  const teamsCountBadge = document.getElementById("teams-count-badge");

  // Questions
  const qCounter = document.getElementById("q-counter");
  const qTextDisplay = document.getElementById("q-text-display");
  const qAnswerBox = document.getElementById("q-answer-box");
  const qAnswerText = document.getElementById("q-answer-text");
  const btnQPrev = document.getElementById("btn-q-prev");
  const btnQNext = document.getElementById("btn-q-next");
  const btnQReveal = document.getElementById("btn-q-reveal");
  const inputExcelFile = document.getElementById("input-excel-file");

  // Modal Settings
  const btnSettingsToggle = document.getElementById("btn-settings-toggle");
  const modalSettings = document.getElementById("modal-settings");
  const btnModalClose = document.getElementById("btn-modal-close");
  const btnModalSave = document.getElementById("btn-modal-save");
  const inputEspIp = document.getElementById("input-esp-ip");
  const inputSonsDossier = document.getElementById("input-sons-dossier");
  const inputSonsFichiers = document.getElementById("input-sons-fichiers");
  const txtSonsStatus = document.getElementById("txt-sons-status");

  // Modal Podium
  const modalPodium = document.getElementById("modal-podium");
  const podiumRankingList = document.getElementById("podium-ranking-list");
  const btnPodiumRestart = document.getElementById("btn-podium-restart");
  const btnPodiumContinue = document.getElementById("btn-podium-continue");
  const btnEndGame = document.getElementById("btn-end-game");

  // Formatage Chrono (00:30)
  function formatSeconds(sec) {
    const s = Math.max(0, Math.ceil(sec));
    const m = Math.floor(s / 60);
    const r = s % 60;
    return `${String(m).padStart(2, '0')}:${String(r).padStart(2, '0')}`;
  }

  // --- Rendu de l'état ---
  function render(state, eventType) {
    // 1. Indicateur de connexion
    if (state.connected) {
      connDot.className = "status-dot online";
      badgeStatus.textContent = "Connecté";
      badgeStatus.style.color = "var(--color-success)";
    } else {
      connDot.className = "status-dot";
      badgeStatus.textContent = "Wi-Fi Déconnecté";
      badgeStatus.style.color = "var(--text-muted)";
    }

    // 2. Valeur des points
    displayPtsValeur.textContent = state.pointsCourants;
    btnValiderSub.textContent = `+${state.pointsCourants} POINT${state.pointsCourants > 1 ? 'S' : ''}`;

    // 2bis. Bouton Chrono rapide
    const txtChronoBtnLabel = document.getElementById("txt-chrono-btn-label");
    if (txtChronoBtnLabel) {
      txtChronoBtnLabel.textContent = state.chrono.enabled ? `Chrono : ${state.chrono.durationSec}s` : "Temps Libre (Illimité)";
    }

    // 3. Zone HERO BUZZ
    if (state.state === "BUZZED" && state.currentTeam !== null) {
      const team = state.equipes[state.currentTeam];
      const teamColor = hub.getTeamColor(state.currentTeam);
      const gradientCss = hub.getTeamGradientCss(state.currentTeam);
      
      buzzHero.classList.add("state-buzzed", "multi-color-glow");
      buzzHero.style.setProperty("--buzz-color", teamColor);
      buzzHero.style.background = gradientCss;

      buzzStatusText.textContent = "EN ÉCOUTE";
      
      const buzzTeamPaletteLabel = document.getElementById("buzz-team-palette-label");
      if (buzzTeamPaletteLabel) {
        buzzTeamPaletteLabel.textContent = hub.getTeamFullLabel(state.currentTeam);
        buzzTeamPaletteLabel.style.display = "block";
      }

      buzzTeamName.textContent = team ? team.nom : `Équipe ${state.currentTeam + 1}`;
      buzzTeamName.style.color = "#FFFFFF";
      buzzTeamName.classList.add("multi-color-text");

      buzzActionRow.style.display = "grid";

      // Chrono Optionnel ou Temps Libre
      if (state.chrono.enabled) {
        buzzTimerDisplay.textContent = formatSeconds(state.chrono.remaining);
        if (state.chrono.remaining <= 3 && state.chrono.remaining > 0) {
          buzzTimerDisplay.classList.add("urgent");
        } else {
          buzzTimerDisplay.classList.remove("urgent");
        }
      } else {
        buzzTimerDisplay.textContent = `⏱️ +${state.chrono.elapsed || 0}s`;
        buzzTimerDisplay.classList.remove("urgent");
      }
    } else {
      // Mode IDLE / En attente
      buzzHero.classList.remove("state-buzzed", "multi-color-glow");
      buzzHero.style.removeProperty("--buzz-color");
      buzzHero.style.background = "var(--bg-card)";

      buzzStatusText.textContent = "EN ATTENTE D'UN BUZZ";
      const buzzTeamPaletteLabel = document.getElementById("buzz-team-palette-label");
      if (buzzTeamPaletteLabel) {
        buzzTeamPaletteLabel.style.display = "none";
      }

      buzzTeamName.textContent = "Préparez-vous...";
      buzzTeamName.style.color = "var(--text-primary)";
      buzzTeamName.classList.remove("multi-color-text");

      if (state.chrono.enabled) {
        buzzTimerDisplay.textContent = formatSeconds(state.chrono.durationSec);
      } else {
        buzzTimerDisplay.textContent = "TEMPS LIBRE";
      }
      buzzTimerDisplay.classList.remove("urgent");
      buzzActionRow.style.display = "none";
    }

    // 4. Modal Podium
    if (state.state === "FINISHED") {
      modalPodium.style.display = "flex";
      renderPodium(state);
    } else {
      modalPodium.style.display = "none";
    }

    // 5. Grille des équipes & scores
    renderTeamsGrid(state);

    // 6. Questions
    renderQuestions(state);
  }

  // Rendu de la grille des scores
  function renderTeamsGrid(state) {
    teamsCountBadge.textContent = `${state.equipes.length} équipes`;
    teamsGrid.innerHTML = "";

    state.equipes.forEach((eq, idx) => {
      const card = document.createElement("div");
      const isBuzzed = state.currentTeam === idx;
      const isBanned = state.bannedTeams.includes(idx);
      const teamColor = hub.getTeamColor(idx);

      card.className = "team-card" + (isBuzzed ? " is-active" : "");
      card.style.setProperty("--team-color", teamColor);
      if (isBanned) card.style.opacity = "0.5";

      // Palette de N couleurs choisies par l'animateur
      const palette = hub.getAnimatorPalette(idx);
      const gradientCss = hub.getTeamGradientCss(idx);
      const paletteLabel = hub.getTeamFullLabel(idx);

      let swatchesHtml = "";
      palette.forEach((color, cIdx) => {
        swatchesHtml += `
          <label title="Modifier couleur ${cIdx + 1}" style="cursor: pointer; position: relative; display: inline-flex; width: 26px; height: 26px; align-items: center; justify-content: center;">
            <input type="color" value="${color}" data-color-idx="${cIdx}" data-team-id="${idx}" style="opacity: 0; width: 26px; height: 26px; position: absolute; cursor: pointer;">
            <div class="team-color-indicator" style="background-color: ${color}; box-shadow: 0 0 6px ${color}; width: 16px; height: 16px; cursor: pointer;"></div>
          </label>
        `;
      });

      card.innerHTML = `
        <div class="team-card-head">
          <div style="display: flex; flex-direction: column; align-items: flex-start; gap: 2px; min-width: 56px; flex: 1 1 56px;">
            <span style="font-size: 0.72rem; font-weight: 800; color: var(--color-gold); text-transform: uppercase; letter-spacing: 0.5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 120px;" title="${paletteLabel}">
              ${paletteLabel}
            </span>
            <input type="text" class="team-name-input" value="${eq.nom}" data-rename-team="${idx}" title="Renommer l'équipe" style="background: transparent; border: none; border-bottom: 1px dashed rgba(255,255,255,0.2); color: #fff; font-weight: 700; font-size: 0.95rem; width: 100%; outline: none; padding: 0;">
          </div>
          <div style="display: flex; align-items: center; flex-wrap: wrap; gap: 4px; flex-shrink: 0; max-width: 100%;">
            ${swatchesHtml}
            <button class="btn-palette-adjust" data-anim-add-color="${idx}" title="Ajouter une couleur à cette équipe" style="width: 28px; height: 28px; font-size: 0.95rem; border-radius: 6px; padding: 0; line-height: 1;">+</button>
            <button class="btn-palette-adjust" data-anim-sub-color="${idx}" title="Retirer une couleur" style="width: 28px; height: 28px; font-size: 0.95rem; border-radius: 6px; padding: 0; line-height: 1; ${palette.length <= 1 ? 'display:none;' : ''}">−</button>
          </div>
        </div>

        <div style="height: 3px; border-radius: 2px; width: 100%; margin: 5px 0; background: ${gradientCss};" class="multi-color-glow"></div>

        <div class="team-score" data-edit-score="${idx}" title="Cliquer pour modifier le score directement" style="cursor: pointer;">${eq.score}</div>
        <div class="team-actions">
          <button class="btn-score-adjust" data-team="${idx}" data-delta="-1" title="Retirer 1 point">−</button>
          <button class="btn-score-adjust" data-team="${idx}" data-delta="1" title="Ajouter 1 point">+</button>
        </div>
      `;

      teamsGrid.appendChild(card);
    });

    // Écouteurs sur les couleurs d'affichage animateur
    teamsGrid.querySelectorAll("[data-color-idx]").forEach(input => {
      input.onchange = (e) => {
        const tid = parseInt(input.dataset.teamId, 10);
        const cIdx = parseInt(input.dataset.colorIdx, 10);
        hub.setAnimatorColor(tid, cIdx, e.target.value);
      };
    });

    // Écouteur bouton + pour ajouter une couleur à l'équipe
    teamsGrid.querySelectorAll("[data-anim-add-color]").forEach(btn => {
      btn.onclick = (e) => {
        e.stopPropagation();
        const tid = parseInt(btn.dataset.animAddColor, 10);
        const palette = hub.getAnimatorPalette(tid);
        const defaultPalette = ["#EF4444", "#3B82F6", "#10B981", "#F59E0B", "#EC4899", "#06B6D4", "#F97316", "#8B5CF6"];
        const nextColor = defaultPalette[palette.length % defaultPalette.length];
        hub.addAnimatorColor(tid, nextColor);
      };
    });

    // Écouteur bouton - pour retirer une couleur
    teamsGrid.querySelectorAll("[data-anim-sub-color]").forEach(btn => {
      btn.onclick = (e) => {
        e.stopPropagation();
        const tid = parseInt(btn.dataset.animSubColor, 10);
        hub.removeAnimatorColor(tid);
      };
    });

    // Écouteurs sur le renommage d'équipe au vol
    document.querySelectorAll("[data-rename-team]").forEach(input => {
      input.onchange = (e) => {
        const tid = parseInt(input.dataset.renameTeam, 10);
        hub.setTeamName(tid, e.target.value);
      };
    });

    // Écouteurs sur les boutons + et - de chaque équipe
    teamsGrid.querySelectorAll(".btn-score-adjust").forEach(btn => {
      btn.onclick = (e) => {
        e.stopPropagation();
        const tid = parseInt(btn.dataset.team, 10);
        const delta = parseInt(btn.dataset.delta, 10);
        hub.adjustScore(tid, delta);
      };
    });

    // Écouteur sur le clic direct sur le score pour le modifier
    teamsGrid.querySelectorAll("[data-edit-score]").forEach(el => {
      el.onclick = (e) => {
        e.stopPropagation();
        const tid = parseInt(el.dataset.editScore, 10);
        const current = hub.state.equipes[tid] ? hub.state.equipes[tid].score : 0;
        hub.uiPrompt(`Modifier le score de ${hub.state.equipes[tid].nom} :`, current, { type: "number" }).then((val) => {
          if (val !== null) hub.setTeamScore(tid, val);
        });
      };
    });
  }

  // Rendu des questions
  function renderQuestions(state) {
    const qCount = state.questions.length;
    if (qCount === 0) {
      qCounter.textContent = "Aucune question";
      qTextDisplay.textContent = "Importez un fichier Excel ou posez vos questions à l'oral.";
      btnQReveal.style.display = "none";
      qAnswerBox.style.display = "none";
      return;
    }

    const currIdx = state.currentQuestionIndex || 0;
    qCounter.textContent = `Question ${currIdx + 1} / ${qCount}`;
    const currQ = state.questions[currIdx];

    if (currQ) {
      qTextDisplay.textContent = currQ.question || currQ.intitule || "Question sans texte";
      qAnswerText.textContent = currQ.reponse || currQ.solution || "—";
      btnQReveal.style.display = "inline-flex";
    }
  }

  // Rendu du podium
  function renderPodium(state) {
    const sorted = [...state.equipes].sort((a, b) => b.score - a.score);
    podiumRankingList.innerHTML = "";

    sorted.forEach((team, i) => {
      let badge = `${i + 1}e`;
      let color = "var(--text-secondary)";
      if (i === 0) { badge = "🥇 1er"; color = "var(--color-gold)"; }
      else if (i === 1) { badge = "🥈 2e"; color = "#E2E8F0"; }
      else if (i === 2) { badge = "🥉 3e"; color = "#F97316"; }

      const row = document.createElement("div");
      row.style.display = "flex";
      row.style.alignItems = "center";
      row.style.justifyContent = "space-between";
      row.style.padding = "10px 14px";
      row.style.borderRadius = "var(--radius-sm)";
      row.style.background = "rgba(255, 255, 255, 0.04)";

      row.innerHTML = `
        <div style="display: flex; align-items: center; gap: 10px;">
          <span style="font-weight: 800; color: ${color}; min-width: 45px;">${badge}</span>
          <span style="font-weight: 600;">${team.nom}</span>
        </div>
        <span class="mono" style="font-size: 1.2rem; font-weight: 800; color: ${color};">${team.score} pts</span>
      `;
      podiumRankingList.appendChild(row);
    });
  }

  // --- Événements boutons tactiles ---

  // Valider (Pouce gauche)
  btnValider.addEventListener("click", () => {
    hub.validerReponse();
  });

  // Refuser (Pouce droit)
  btnRefuser.addEventListener("click", () => {
    hub.refuserReponse();
  });

  // Ajustement de la valeur de la question (+ / - et clic direct)
  btnPtsMinus.addEventListener("click", () => {
    hub.setQuestionValue(hub.state.pointsCourants - 1);
  });

  btnPtsPlus.addEventListener("click", () => {
    hub.setQuestionValue(hub.state.pointsCourants + 1);
  });

  if (displayPtsValeur) {
    displayPtsValeur.style.cursor = "pointer";
    displayPtsValeur.title = "Cliquer pour saisir directement une valeur";
    displayPtsValeur.addEventListener("click", () => {
      hub.uiPrompt("Valeur de la question (points) :", hub.state.pointsCourants, { type: "number" }).then((val) => {
        if (val !== null && val.trim() !== "") hub.setQuestionValue(val);
      });
    });
  }

  // Remise à 0 des scores
  const btnResetScores = document.getElementById("btn-reset-scores");
  if (btnResetScores) {
    btnResetScores.addEventListener("click", () => {
      hub.uiConfirm("Voulez-vous remettre à 0 les scores de TOUTES les équipes ?", { danger: true }).then((ok) => {
        if (ok) hub.resetScores();
      });
    });
  }
  // Bascule rapide du Chrono (15s -> 20s -> 30s -> 45s -> 60s -> Temps Libre -> 15s)
  const btnToggleChrono = document.getElementById("btn-toggle-chrono");
  if (btnToggleChrono) {
    btnToggleChrono.addEventListener("click", () => {
      const presets = [15, 20, 30, 45, 60, null]; // null = temps libre désactivé
      let nextIndex = 0;
      if (hub.state.chrono.enabled) {
        const curr = hub.state.chrono.durationSec;
        const idx = presets.indexOf(curr);
        nextIndex = (idx >= 0 && idx < presets.length - 1) ? idx + 1 : 0;
      } else {
        nextIndex = 0; // réactive à 15s
      }

      const nextVal = presets[nextIndex];
      if (nextVal === null) {
        hub.toggleChrono(false);
      } else {
        hub.toggleChrono(true);
        hub.setChronoDuration(nextVal);
      }
    });
  }

  // Simulation de buzz manuel (Menu rapide)
  document.getElementById("btn-manual-buzz").addEventListener("click", () => {
    const activeTeams = hub.state.equipes.map((e, idx) => ({ id: idx, nom: e.nom }))
      .filter(e => !hub.state.bannedTeams.includes(e.id));

    if (activeTeams.length === 0) {
      hub.uiAlert("Toutes les équipes ont déjà buzzé sur cette question !");
      return;
    }
    hub.uiChoose("Qui a buzzé ?", activeTeams.map(t => ({ label: t.nom, value: t.id }))).then((tid) => {
      if (tid !== null && tid >= 0 && tid < hub.state.equipes.length) {
        hub.triggerBuzz(tid);
      }
    });
  });

  // Clôturer la partie (Podium)
  btnEndGame.addEventListener("click", () => {
    hub.uiConfirm("Terminer la partie et afficher le podium ?").then((ok) => {
      if (ok) hub.finishGame();
    });
  });

  btnPodiumRestart.addEventListener("click", () => {
    hub.uiConfirm("Réinitialiser tous les scores à 0 ?", { danger: true }).then((ok) => {
      if (ok) hub.resetGame();
    });
  });

  btnPodiumContinue.addEventListener("click", () => {
    hub.continueGame();
  });

  // Révéler / masquer la réponse question
  btnQReveal.addEventListener("click", () => {
    qAnswerBox.style.display = (qAnswerBox.style.display === "none") ? "block" : "none";
  });

  // Question précédente / suivante
  btnQPrev.addEventListener("click", () => {
    if (hub.state.questions.length > 0) {
      hub.state.currentQuestionIndex = Math.max(0, (hub.state.currentQuestionIndex || 0) - 1);
      qAnswerBox.style.display = "none";
      hub._notify("QUESTION");
    }
  });

  btnQNext.addEventListener("click", () => {
    if (hub.state.questions.length > 0) {
      hub.state.currentQuestionIndex = Math.min(hub.state.questions.length - 1, (hub.state.currentQuestionIndex || 0) + 1);
      qAnswerBox.style.display = "none";
      hub._notify("QUESTION");
    }
  });

  // Import de questions Excel/CSV
  inputExcelFile.addEventListener("change", (e) => {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (evt) => {
      try {
        const data = new Uint8Array(evt.target.result);
        const workbook = XLSX.read(data, { type: "array" });
        const firstSheet = workbook.Sheets[workbook.SheetNames[0]];
        const json = XLSX.utils.sheet_to_json(firstSheet);
        if (Array.isArray(json) && json.length > 0) {
          hub.state.questions = json.map(r => ({
            question: r.Question || r.question || r.Intitule || Object.values(r)[0],
            reponse: r.Reponse || r.reponse || r.Solution || Object.values(r)[1] || ""
          }));
          hub.state.currentQuestionIndex = 0;
          hub._notify("LOAD_QUESTIONS");
          hub.uiAlert(`${hub.state.questions.length} questions importées avec succès !`);
        }
      } catch (err) {
        hub.uiAlert("Erreur lors de la lecture du fichier Excel/CSV.");
      }
    };
    reader.readAsArrayBuffer(file);
  });

  // Plein écran
  document.getElementById("btn-fullscreen").addEventListener("click", () => {
    if (!document.fullscreenElement) {
      document.documentElement.requestFullscreen().catch(() => {});
    } else {
      document.exitFullscreen().catch(() => {});
    }
  });

  // Modal Paramètres IP ESP32
  btnSettingsToggle.addEventListener("click", () => {
    inputEspIp.value = hub.getEsp32Ip();
    modalSettings.style.display = "flex";
  });

  btnModalClose.addEventListener("click", () => {
    modalSettings.style.display = "none";
  });

  btnModalSave.addEventListener("click", () => {
    hub.setEsp32Ip(inputEspIp.value);
    modalSettings.style.display = "none";
  });

  // Chargement des sons personnalisés (buzz.mp3, victoire.mp3, echec.mp3,
  // equipe_N.mp3 — même convention de noms que le dossier sounds/ du logiciel PC).
  // Deux entrées pour le même traitement : "webkitdirectory" (dossier entier)
  // marche sur PC/Android/Safari 18.4+ (iOS/iPadOS recent, via l'app Fichiers).
  // Le second input (fichiers un par un) reste un repli universel si jamais
  // un appareil plus ancien ne supporte pas la selection de dossier.
  function onSonsFilesChange(e) {
    const files = e.target.files;
    if (!files || files.length === 0) return;
    const { reconnus, ignores } = hub.chargerSonsPersonnalises(files);
    const lignes = [];
    if (reconnus.length) lignes.push(`✔ ${reconnus.join(" · ")}`);
    if (ignores.length) lignes.push(`Ignorés (nom non reconnu) : ${ignores.join(", ")}`);
    if (txtSonsStatus) {
      txtSonsStatus.textContent = lignes.join(" — ") || "Aucun fichier reconnu.";
      txtSonsStatus.style.color = reconnus.length ? "var(--color-success)" : "var(--color-danger)";
    }
  }
  if (inputSonsDossier) inputSonsDossier.addEventListener("change", onSonsFilesChange);
  if (inputSonsFichiers) inputSonsFichiers.addEventListener("change", onSonsFilesChange);

  // Initialisation et souscription aux changements
  hub.subscribe(render);

})();
