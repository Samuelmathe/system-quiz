(function () {
  "use strict";

  var STORAGE_KEY = "regie_quiz_dmx_v1";
  var MAX_EQUIPES = 30;

  var defaultState = {
    equipes: [
      { nom: "Équipe 1", couleurs: ["#ff0000", "#ff0000"], couleursAffichage: ["#ff0000"], score: 0, strobeDureeMs: 400 },
      { nom: "Équipe 2", couleurs: ["#0000ff", "#0000ff"], couleursAffichage: ["#0000ff"], score: 0, strobeDureeMs: 400 },
      { nom: "Équipe 3", couleurs: ["#00c853", "#00c853"], couleursAffichage: ["#00c853"], score: 0, strobeDureeMs: 0 },
      { nom: "Équipe 4", couleurs: ["#ffd600", "#ffd600"], couleursAffichage: ["#ffd600"], score: 0, strobeDureeMs: 0 },
      { nom: "Équipe 5", couleurs: ["#ff0080", "#ff0080"], couleursAffichage: ["#ff0080"], score: 0, strobeDureeMs: -1 },
      { nom: "Équipe 6", couleurs: ["#00e5ff", "#00e5ff"], couleursAffichage: ["#00e5ff"], score: 0, strobeDureeMs: 0 }
    ],
    projecteurs: [
      {
        id: 1, adresse: 51, nbCanaux: 8, mode: 0,
        offDim: 0, offR: 1, offG: 2, offB: 3,
        offStrobe: -1, strobeValue: 200, strobeRepos: 0
      },
      {
        id: 2, adresse: 59, nbCanaux: 17, mode: 1,
        offDim: 4, offR: 8, offG: 2, offB: 3,
        offStrobe: 5, strobeValue: 100, strobeRepos: 255
      }
    ],
    questions: [],
    currentQuestionIndex: 0,
    pointsCourants: 1,
    pointsAuBuzz: 0,
    chrono: { enabled: true, durationSec: 30, timeoutFaux: true }
  };

  function loadState() {
    try {
      var raw = localStorage.getItem(STORAGE_KEY);
      if (raw) return JSON.parse(raw);
    } catch (e) {}
    return JSON.parse(JSON.stringify(defaultState));
  }

  function saveState() {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(state)); } catch (e) {}
  }

  var state = loadState();
  if (!Array.isArray(state.questions)) state.questions = JSON.parse(JSON.stringify(defaultState.questions));
  if (typeof state.currentQuestionIndex !== "number") state.currentQuestionIndex = 0;
  if (typeof state.pointsCourants !== "number") state.pointsCourants = 1;
  if (typeof state.pointsAuBuzz !== "number") state.pointsAuBuzz = 0;
  if (!state.chrono) state.chrono = { enabled: true, durationSec: 30, timeoutFaux: true };

  // ---------------- Onglets ----------------
  var tabConfig = document.getElementById("tab-config");
  var tabScore = document.getElementById("tab-score");
  var viewConfig = document.getElementById("view-config");
  var viewScore = document.getElementById("view-score");

  function showTab(name) {
    var isConfig = name === "config";
    viewConfig.hidden = !isConfig;
    viewScore.hidden = isConfig;
    tabConfig.classList.toggle("active", isConfig);
    tabScore.classList.toggle("active", !isConfig);
    if (!isConfig) renderAnimateur();
  }
  tabConfig.addEventListener("click", function () { showTab("config"); });
  tabScore.addEventListener("click", function () { showTab("score"); });

  // Le bandeau "équipe en écoute" reste collé juste sous l'en-tête en scrollant —
  // utile sur mobile avec beaucoup d'équipes (jusqu'à 30) dans la grille.
  function updateMastheadOffset() {
    var masthead = document.querySelector("header.masthead");
    if (masthead) {
      document.documentElement.style.setProperty("--masthead-h", masthead.offsetHeight + "px");
    }
  }
  window.addEventListener("resize", updateMastheadOffset);
  updateMastheadOffset();

  // ---------------- Utilitaires couleur ----------------
  function hexToRgb(hex) {
    var m = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
    return m ? { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16) } : { r: 255, g: 255, b: 255 };
  }

  var ROUE = [
    { nom: "Blanc", r: 255, g: 255, b: 255 },
    { nom: "Rouge", r: 255, g: 0, b: 0 },
    { nom: "Vert", r: 0, g: 255, b: 0 },
    { nom: "Bleu", r: 0, g: 0, b: 255 },
    { nom: "Jaune", r: 255, g: 255, b: 0 },
    { nom: "Rose", r: 255, g: 0, b: 128 },
    { nom: "Orange", r: 255, g: 128, b: 0 },
    { nom: "Cyan", r: 0, g: 255, b: 255 }
  ];

  function couleurRoueLaPlusProche(hex) {
    var c = hexToRgb(hex);
    var meilleure = ROUE[0], meilleureDist = Infinity;
    for (var i = 0; i < ROUE.length; i++) {
      var d = Math.pow(c.r - ROUE[i].r, 2) + Math.pow(c.g - ROUE[i].g, 2) + Math.pow(c.b - ROUE[i].b, 2);
      if (d < meilleureDist) { meilleureDist = d; meilleure = ROUE[i]; }
    }
    return meilleure.nom;
  }

  // ---------------- Rendu : projecteurs ----------------
  var fixturesGrid = document.getElementById("fixtures-grid");

  function champNombre(label, valeur, onChange, opts) {
    opts = opts || {};
    var row = document.createElement("div");
    row.className = "field-row";
    var lab = document.createElement("label");
    lab.textContent = label;
    var input = document.createElement("input");
    input.type = "number";
    input.value = valeur;
    if (opts.min !== undefined) input.min = opts.min;
    input.addEventListener("input", function () {
      onChange(parseInt(input.value, 10) || 0);
      saveState();
    });
    row.appendChild(lab);
    row.appendChild(input);
    return row;
  }

  function renderFixtureCard(fix, index) {
    var card = document.createElement("div");
    card.className = "card";

    var head = document.createElement("div");
    head.className = "card-head";
    var h3 = document.createElement("h3");
    h3.textContent = "Projecteur " + (index + 1);
    var pill = document.createElement("span");
    pill.className = "mode-pill";
    pill.textContent = fix.mode === 1 ? "ROUE" : "RGB";
    head.appendChild(h3);
    head.appendChild(pill);
    card.appendChild(head);

    card.appendChild(champNombre("Adresse DMX", fix.adresse, function (v) { fix.adresse = v; }, { min: 1 }));
    card.appendChild(champNombre("Nombre de canaux", fix.nbCanaux, function (v) { fix.nbCanaux = v; }, { min: 1 }));

    var modeRow = document.createElement("div");
    modeRow.className = "field-row";
    var select = document.createElement("select");
    select.className = "mode-select";
    select.innerHTML = '<option value="0">RGB continu</option><option value="1">Roue de couleurs (lyre)</option>';
    select.value = String(fix.mode);
    select.addEventListener("change", function () {
      fix.mode = parseInt(select.value, 10);
      saveState();
      renderFixtures();
    });
    modeRow.appendChild(select);
    card.appendChild(modeRow);

    card.appendChild(champNombre("Offset DIMMER", fix.offDim, function (v) { fix.offDim = v; }));

    if (fix.mode === 1) {
      card.appendChild(champNombre("Offset canal ROUE", fix.offR, function (v) { fix.offR = v; }));
    } else {
      card.appendChild(champNombre("Offset ROUGE", fix.offR, function (v) { fix.offR = v; }));
      card.appendChild(champNombre("Offset VERT", fix.offG, function (v) { fix.offG = v; }));
      card.appendChild(champNombre("Offset BLEU", fix.offB, function (v) { fix.offB = v; }));
    }

    card.appendChild(champNombre("Offset STROBE (-1 = aucun)", fix.offStrobe, function (v) { fix.offStrobe = v; }));
    card.appendChild(champNombre("Valeur STROBE", fix.strobeValue, function (v) { fix.strobeValue = v; }));
    card.appendChild(champNombre("Valeur repos obturateur", fix.strobeRepos, function (v) { fix.strobeRepos = v; }));

    var actions = document.createElement("div");
    actions.className = "card-actions";
    var del = document.createElement("button");
    del.className = "btn subtle";
    del.textContent = "Supprimer";
    del.addEventListener("click", function () {
      state.projecteurs.splice(index, 1);
      syncEquipeColors();
      saveState();
      renderFixtures();
      renderEquipesConfig();
    });
    actions.appendChild(del);
    card.appendChild(actions);

    return card;
  }

  function renderFixtures() {
    fixturesGrid.innerHTML = "";
    state.projecteurs.forEach(function (fix, i) {
      fixturesGrid.appendChild(renderFixtureCard(fix, i));
    });
    var addBtn = document.createElement("button");
    addBtn.className = "add-fixture";
    addBtn.textContent = "+ Ajouter un projecteur";
    addBtn.addEventListener("click", function () {
      var last = state.projecteurs[state.projecteurs.length - 1];
      state.projecteurs.push(Object.assign({}, last, { id: state.projecteurs.length + 1 }));
      syncEquipeColors();
      saveState();
      renderFixtures();
      renderEquipesConfig();
    });
    fixturesGrid.appendChild(addBtn);
  }

  // ---------------- Rendu : équipes (config) ----------------
  var equipesList = document.getElementById("equipes-list");
  var equipesHeader = document.getElementById("equipes-header");

  function syncEquipeColors() {
    state.equipes.forEach(function (eq) {
      if (!Array.isArray(eq.couleurs)) {
        var base = eq.couleur || "#ffffff";
        eq.couleurs = state.projecteurs.map(function () { return base; });
        delete eq.couleur;
      }
      while (eq.couleurs.length < state.projecteurs.length) {
        eq.couleurs.push(eq.couleurs[eq.couleurs.length - 1] || "#ffffff");
      }
      eq.couleurs.length = state.projecteurs.length;
      if (!Array.isArray(eq.couleursAffichage) || !eq.couleursAffichage.length) {
        eq.couleursAffichage = [eq.couleurAffichage || eq.couleurs[0] || "#ffffff"];
        delete eq.couleurAffichage;
      }
    });
  }

  // Couleur(s) saisie(s) par l'animateur pour son propre écran (score, écran public) —
  // une équipe peut en avoir plusieurs (ex : projecteurs de couleurs différentes) —
  // indépendantes des couleurs par projecteur configurées côté DMX : elles n'influencent
  // jamais le patch envoyé au Mega. La première sert de couleur d'identité principale.
  function primaryColor(eq) {
    return eq.couleursAffichage[0] || "#ffffff";
  }

  function renderEquipesHeader() {
    equipesHeader.innerHTML = "";
    var hNom = document.createElement("span");
    hNom.className = "h-nom";
    hNom.textContent = "Équipe";
    var hCouleurs = document.createElement("div");
    hCouleurs.className = "h-couleurs";
    state.projecteurs.forEach(function (fix, i) {
      var span = document.createElement("span");
      span.textContent = "P" + (i + 1);
      hCouleurs.appendChild(span);
    });
    var hStrobe = document.createElement("span");
    hStrobe.className = "h-strobe";
    hStrobe.textContent = "Strobe (ms)";
    equipesHeader.appendChild(hNom);
    equipesHeader.appendChild(hCouleurs);
    equipesHeader.appendChild(hStrobe);
  }

  function renderEquipesConfig() {
    syncEquipeColors();
    renderEquipesHeader();
    equipesList.innerHTML = "";
    state.equipes.forEach(function (eq, i) {
      var row = document.createElement("div");
      row.className = "equipe-row";

      var nom = document.createElement("input");
      nom.type = "text";
      nom.className = "equipe-name-input";
      nom.value = eq.nom;
      nom.addEventListener("input", function () { eq.nom = nom.value; saveState(); });

      var colorsWrap = document.createElement("div");
      colorsWrap.className = "equipe-colors";
      eq.couleurs.forEach(function (hex, p) {
        var chip = document.createElement("div");
        chip.className = "equipe-color-chip";
        var input = document.createElement("input");
        input.type = "color";
        input.value = hex;
        input.title = "Couleur pour le projecteur " + (p + 1);
        input.addEventListener("input", function () {
          eq.couleurs[p] = input.value;
          saveState();
          if (p === 0) renderAnimateur();
        });
        var label = document.createElement("span");
        label.textContent = "P" + (p + 1);
        chip.appendChild(input);
        chip.appendChild(label);
        colorsWrap.appendChild(chip);
      });

      var strobeWrap = document.createElement("div");
      strobeWrap.className = "strobe-mini";
      var strobeInput = document.createElement("input");
      strobeInput.type = "number";
      strobeInput.value = eq.strobeDureeMs;
      strobeInput.addEventListener("input", function () {
        eq.strobeDureeMs = parseInt(strobeInput.value, 10) || 0;
        saveState();
      });
      var strobeLabel = document.createElement("span");
      strobeLabel.textContent = "-1=continu";
      strobeWrap.appendChild(strobeInput);
      strobeWrap.appendChild(strobeLabel);

      var del = document.createElement("button");
      del.className = "btn subtle";
      del.textContent = "Retirer";
      del.addEventListener("click", function () {
        state.equipes.splice(i, 1);
        saveState();
        renderEquipesConfig();
      });

      row.appendChild(nom);
      row.appendChild(colorsWrap);
      row.appendChild(strobeWrap);
      row.appendChild(del);
      equipesList.appendChild(row);
    });

    var addBtn = document.getElementById("btn-add-equipe");
    var atMax = state.equipes.length >= MAX_EQUIPES;
    addBtn.disabled = atMax;
    document.getElementById("equipes-count").textContent =
      state.equipes.length + " / " + MAX_EQUIPES + " équipes" + (atMax ? " — limite atteinte" : "");
  }

  document.getElementById("btn-add-equipe").addEventListener("click", function () {
    if (state.equipes.length >= MAX_EQUIPES) return;
    state.equipes.push({
      nom: "Équipe " + (state.equipes.length + 1),
      couleurs: state.projecteurs.map(function () { return "#ffffff"; }),
      couleursAffichage: ["#ffffff"],
      score: 0,
      strobeDureeMs: 0
    });
    saveState();
    renderEquipesConfig();
  });

  document.getElementById("btn-reset").addEventListener("click", function () {
    state = JSON.parse(JSON.stringify(defaultState));
    saveState();
    renderFixtures();
    renderEquipesConfig();
    document.getElementById("input-points-buzz").value = state.pointsAuBuzz;
    document.getElementById("chk-chrono-enabled").checked = state.chrono.enabled;
    document.getElementById("input-chrono-duration").value = state.chrono.durationSec;
    document.getElementById("chk-chrono-timeout").checked = state.chrono.timeoutFaux;
    document.getElementById("questions-status").textContent = "Aucun fichier chargé";
    resetGame();
    renderQuestionPanel();
    animLive.hidden = false;
    animPodium.hidden = true;
  });

  document.getElementById("btn-sync").addEventListener("click", function (e) {
    var btn = e.currentTarget;
    var original = btn.textContent;
    btn.textContent = "Synchronisation...";
    btn.disabled = true;
    setTimeout(function () {
      btn.textContent = "Synchronisé ✓";
      setTimeout(function () { btn.textContent = original; btn.disabled = false; }, 1200);
    }, 700);
  });

  // ---------------- Export / import de la configuration ----------------
  var DEFAULT_IO_STATUS = "Modifications enregistrées localement";
  var configIoStatus = document.getElementById("config-io-status");

  function flashConfigIoStatus(text) {
    configIoStatus.textContent = text;
    setTimeout(function () { configIoStatus.textContent = DEFAULT_IO_STATUS; }, 4000);
  }

  function buildConfigExport() {
    return {
      equipes: state.equipes,
      projecteurs: state.projecteurs,
      questions: state.questions,
      currentQuestionIndex: state.currentQuestionIndex,
      pointsCourants: state.pointsCourants,
      pointsAuBuzz: state.pointsAuBuzz,
      chrono: state.chrono
    };
  }

  document.getElementById("btn-export-config").addEventListener("click", function () {
    document.getElementById("export-textarea").value = JSON.stringify(buildConfigExport(), null, 2);
    document.getElementById("export-modal").hidden = false;
  });
  document.getElementById("btn-close-export").addEventListener("click", function () {
    document.getElementById("export-modal").hidden = true;
  });
  document.getElementById("btn-copy-config").addEventListener("click", function () {
    var ta = document.getElementById("export-textarea");
    ta.focus();
    ta.select();
    function onOk() { flashConfigIoStatus("Configuration copiée dans le presse-papiers"); }
    function onFail() { flashConfigIoStatus("Copie automatique indisponible — sélection faite, utilisez Ctrl+C"); }
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(ta.value).then(onOk, onFail);
    } else {
      try {
        var ok = document.execCommand("copy");
        ok ? onOk() : onFail();
      } catch (e) { onFail(); }
    }
  });

  document.getElementById("btn-load-config").addEventListener("click", function () {
    document.getElementById("input-load-config").click();
  });
  document.getElementById("input-load-config").addEventListener("change", function (e) {
    var file = e.target.files && e.target.files[0];
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function (ev) {
      try {
        var data = JSON.parse(ev.target.result);
        if (!Array.isArray(data.equipes) || !Array.isArray(data.projecteurs)) {
          throw new Error("format inattendu");
        }
        state.equipes = data.equipes.slice(0, MAX_EQUIPES);
        state.projecteurs = data.projecteurs;
        state.questions = Array.isArray(data.questions) ? data.questions : [];
        state.currentQuestionIndex = typeof data.currentQuestionIndex === "number" ? data.currentQuestionIndex : 0;
        state.pointsCourants = typeof data.pointsCourants === "number" ? data.pointsCourants : 1;
        state.pointsAuBuzz = typeof data.pointsAuBuzz === "number" ? data.pointsAuBuzz : 0;
        state.chrono = data.chrono || { enabled: true, durationSec: 30, timeoutFaux: true };
        syncEquipeColors();
        saveState();

        renderFixtures();
        renderEquipesConfig();
        document.getElementById("input-points-buzz").value = state.pointsAuBuzz;
        document.getElementById("chk-chrono-enabled").checked = state.chrono.enabled;
        document.getElementById("input-chrono-duration").value = state.chrono.durationSec;
        document.getElementById("chk-chrono-timeout").checked = state.chrono.timeoutFaux;
        document.getElementById("questions-status").textContent = state.questions.length
          ? state.questions.length + " question(s) chargée(s) depuis la configuration"
          : "Aucun fichier chargé";
        animLive.hidden = false;
        animPodium.hidden = true;
        resetQuestion();

        flashConfigIoStatus("Configuration chargée — " + file.name);
        addLog("Configuration importée depuis " + file.name, "c-ok");
      } catch (err) {
        flashConfigIoStatus("Fichier de configuration invalide");
      }
      e.target.value = "";
    };
    reader.readAsText(file);
  });

  // ---------------- Animateur : état de partie (non persisté) ----------------
  var gameState = "idle"; // idle | buzzed | finished
  var currentTeam = null;
  var bannedTeams = [];
  var chronoDeadline = null;
  var chronoTimer = null;
  var logLines = [];
  var publicOpen = false;

  var scoreGrid = document.getElementById("score-grid");
  var statePill = document.getElementById("state-pill");
  var chronoReadout = document.getElementById("chrono-readout");
  var logPanel = document.getElementById("log-panel");
  var animLive = document.getElementById("anim-live");
  var animPodium = document.getElementById("anim-podium");
  var publicOverlay = document.getElementById("public-overlay");
  var publicContent = document.getElementById("public-content");

  function addLog(text, cls) {
    logLines.push({ text: text, cls: cls || "" });
    if (logLines.length > 10) logLines.shift();
    renderLog();
  }

  function renderLog() {
    logPanel.innerHTML = "";
    if (!logLines.length) {
      var empty = document.createElement("div");
      empty.className = "log-empty";
      empty.textContent = "Aucun événement pour l'instant.";
      logPanel.appendChild(empty);
      return;
    }
    logLines.forEach(function (l) {
      var div = document.createElement("div");
      div.className = "log-line" + (l.cls ? " " + l.cls : "");
      div.textContent = l.text;
      logPanel.appendChild(div);
    });
    logPanel.scrollTop = logPanel.scrollHeight;
  }

  function formatChrono(ms) {
    var s = Math.max(0, Math.ceil(ms / 1000));
    var mm = Math.floor(s / 60), ss = s % 60;
    return (mm < 10 ? "0" : "") + mm + ":" + (ss < 10 ? "0" : "") + ss;
  }

  function updateChronoReadout() {
    if (!state.chrono.enabled || chronoDeadline === null) {
      chronoReadout.textContent = "--:--";
      chronoReadout.classList.remove("urgent", "blink");
      return;
    }
    var remaining = chronoDeadline - Date.now();
    chronoReadout.textContent = formatChrono(remaining);
    chronoReadout.classList.toggle("urgent", remaining <= 3000);
    chronoReadout.classList.toggle("blink", remaining <= 3000 && remaining > 0);
    if (publicOpen) renderPublicContent();
  }

  function stopChrono() {
    if (chronoTimer) { clearInterval(chronoTimer); chronoTimer = null; }
    chronoDeadline = null;
    updateChronoReadout();
  }

  function startChrono() {
    if (!state.chrono.enabled) { chronoDeadline = null; updateChronoReadout(); return; }
    chronoDeadline = Date.now() + state.chrono.durationSec * 1000;
    if (chronoTimer) clearInterval(chronoTimer);
    chronoTimer = setInterval(tickChrono, 200);
    updateChronoReadout();
  }

  function tickChrono() {
    if (chronoDeadline === null) return;
    var remaining = chronoDeadline - Date.now();
    if (remaining <= 0) {
      updateChronoReadout();
      if (gameState === "buzzed" && state.chrono.timeoutFaux) {
        addLog("Temps écoulé — mauvaise réponse", "c-warn");
        handleRefuser();
      } else {
        stopChrono();
      }
      return;
    }
    updateChronoReadout();
  }

  function updateStatePill() {
    statePill.classList.remove("buzzed", "finished");
    if (gameState === "buzzed" && currentTeam !== null) {
      statePill.textContent = (state.equipes[currentTeam].nom || "Équipe").toUpperCase() + " EN ÉCOUTE";
      statePill.classList.add("buzzed");
    } else if (gameState === "finished") {
      statePill.textContent = "PARTIE TERMINÉE";
      statePill.classList.add("finished");
    } else {
      statePill.textContent = "EN ATTENTE DE BUZZ";
    }
  }

  function handleBuzz(i) {
    if (gameState !== "idle") return;
    if (bannedTeams.indexOf(i) !== -1) return;
    gameState = "buzzed";
    currentTeam = i;
    bannedTeams.push(i);
    var eq = state.equipes[i];
    if (state.pointsAuBuzz > 0) {
      eq.score += state.pointsAuBuzz;
      addLog("Buzz comptabilisé : +" + state.pointsAuBuzz + " pt(s) — " + eq.nom, "c-ok");
    }
    addLog(eq.nom + " a buzzé !", "c-buzz");
    saveState();
    startChrono();
    renderAnimateur();
  }

  function handleValider() {
    if (gameState !== "buzzed" || currentTeam === null) return;
    var eq = state.equipes[currentTeam];
    var pts = state.pointsCourants;
    eq.score += pts;
    addLog("Bonne réponse ! +" + pts + " pt(s) — " + eq.nom, "c-ok");
    saveState();
    stopChrono();
    resetQuestion();
    nextQuestion();
  }

  function handleRefuser() {
    if (gameState !== "buzzed") return;
    addLog("Mauvaise réponse — " + state.equipes[currentTeam].nom, "c-warn");
    stopChrono();
    gameState = "idle";
    currentTeam = null;
    renderAnimateur();
  }

  function resetQuestion() {
    gameState = "idle";
    currentTeam = null;
    bannedTeams = [];
    stopChrono();
    renderAnimateur();
  }

  function resetGame() {
    resetQuestion();
    state.equipes.forEach(function (eq) { eq.score = 0; });
    state.currentQuestionIndex = 0;
    saveState();
    addLog("Partie réinitialisée", "c-warn");
    renderAnimateur();
  }

  function finishGame() {
    gameState = "finished";
    stopChrono();
    addLog("Fin de partie — classement final", "c-ok");
    animLive.hidden = true;
    animPodium.hidden = false;
    updateStatePill();
    renderPodium();
    if (publicOpen) renderPublicContent();
  }

  function continueGame() {
    gameState = "idle";
    currentTeam = null;
    bannedTeams = [];
    animLive.hidden = false;
    animPodium.hidden = true;
    renderAnimateur();
  }

  // ---------------- Questions (import Excel/CSV) ----------------
  function currentQuestion() {
    if (!state.questions.length) return null;
    var idx = Math.max(0, Math.min(state.currentQuestionIndex, state.questions.length - 1));
    return state.questions[idx];
  }

  function renderQuestionPanel() {
    var panel = document.getElementById("question-panel");
    var pointsInput = document.getElementById("input-current-points");
    var q = currentQuestion();

    panel.hidden = !q;
    if (q) {
      document.getElementById("question-index").textContent =
        "Question " + (state.currentQuestionIndex + 1) + " / " + state.questions.length;
      document.getElementById("question-text").textContent = q.question;
    }
    if (document.activeElement !== pointsInput) pointsInput.value = state.pointsCourants;
    if (publicOpen) renderPublicContent();
  }

  function nextQuestion() {
    if (!state.questions.length) return;
    if (state.currentQuestionIndex < state.questions.length - 1) {
      state.currentQuestionIndex++;
      state.pointsCourants = currentQuestion().points;
      saveState();
    }
    renderQuestionPanel();
  }

  function prevQuestion() {
    if (!state.questions.length) return;
    if (state.currentQuestionIndex > 0) {
      state.currentQuestionIndex--;
      state.pointsCourants = currentQuestion().points;
      saveState();
    }
    renderQuestionPanel();
  }

  function parseQuestionsSheet(rows) {
    if (!rows.length) return [];
    var header = rows[0].map(function (c) { return String(c || "").trim().toLowerCase(); });
    var qCol = header.findIndex(function (h) { return h.indexOf("question") !== -1; });
    var pCol = header.findIndex(function (h) { return h.indexOf("point") !== -1; });
    var startRow = 1;
    if (qCol === -1 && pCol === -1) { qCol = 0; pCol = 1; startRow = 0; }
    if (qCol === -1) qCol = 0;
    if (pCol === -1) pCol = 1;

    var out = [];
    for (var i = startRow; i < rows.length; i++) {
      var row = rows[i];
      if (!row || !row[qCol]) continue;
      var pts = parseInt(row[pCol], 10);
      out.push({ question: String(row[qCol]).trim(), points: isNaN(pts) ? 1 : pts });
    }
    return out;
  }

  function loadQuestionsFromFile(file) {
    var status = document.getElementById("questions-status");
    var reader = new FileReader();
    reader.onload = function (e) {
      try {
        var workbook;
        if (/\.csv$/i.test(file.name)) {
          workbook = XLSX.read(e.target.result, { type: "string" });
        } else {
          workbook = XLSX.read(new Uint8Array(e.target.result), { type: "array" });
        }
        var sheet = workbook.Sheets[workbook.SheetNames[0]];
        var rows = XLSX.utils.sheet_to_json(sheet, { header: 1 });
        var questions = parseQuestionsSheet(rows);
        if (!questions.length) {
          status.textContent = "Aucune question trouvée dans ce fichier";
          return;
        }
        state.questions = questions;
        state.currentQuestionIndex = 0;
        state.pointsCourants = questions[0].points;
        saveState();
        status.textContent = questions.length + " question(s) chargée(s) — " + file.name;
        addLog(questions.length + " questions importées depuis " + file.name, "c-ok");
        renderQuestionPanel();
      } catch (err) {
        status.textContent = "Erreur de lecture du fichier";
      }
    };
    if (/\.csv$/i.test(file.name)) reader.readAsText(file);
    else reader.readAsArrayBuffer(file);
  }

  document.getElementById("btn-load-questions").addEventListener("click", function () {
    document.getElementById("input-questions-file").click();
  });
  document.getElementById("input-questions-file").addEventListener("change", function (e) {
    if (e.target.files && e.target.files[0]) loadQuestionsFromFile(e.target.files[0]);
  });
  document.getElementById("btn-prev-question").addEventListener("click", prevQuestion);
  document.getElementById("btn-next-question").addEventListener("click", nextQuestion);
  document.getElementById("input-current-points").addEventListener("input", function (e) {
    state.pointsCourants = parseInt(e.target.value, 10) || 0;
    saveState();
    if (publicOpen) renderPublicContent();
  });

  function distinctProjectorColors(eq) {
    var seen = {};
    var out = [];
    (eq.couleurs || []).forEach(function (c) {
      var key = (c || "").toLowerCase();
      if (!seen[key]) { seen[key] = true; out.push(c); }
    });
    return out;
  }

  // Rendu CSS des couleurs d'une équipe : un aplat si une seule couleur, sinon un
  // dégradé à bandes nettes qui montre chaque couleur — utilisé partout où l'équipe
  // "a la main" (bandeau animateur, écran public) pour ne perdre aucune couleur.
  function teamColorBackground(eq) {
    var colors = eq.couleursAffichage || [];
    if (colors.length <= 1) return colors[0] || "#ffffff";
    var n = colors.length;
    var stops = [];
    colors.forEach(function (c, i) {
      stops.push(c + " " + ((i / n) * 100).toFixed(2) + "%");
      stops.push(c + " " + (((i + 1) / n) * 100).toFixed(2) + "%");
    });
    return "linear-gradient(135deg, " + stops.join(", ") + ")";
  }

  function renderActiveBuzzBanner() {
    var banner = document.getElementById("active-buzz-banner");
    if (gameState === "buzzed" && currentTeam !== null) {
      var eq = state.equipes[currentTeam];
      banner.hidden = false;
      banner.style.setProperty("--team-color", primaryColor(eq));
      document.getElementById("abb-swatch").style.background = teamColorBackground(eq);
      document.getElementById("abb-team-name").textContent = eq.nom;
    } else {
      banner.hidden = true;
    }
  }

  function renderAnimateur() {
    updateStatePill();
    updateChronoReadout();
    renderQuestionPanel();
    renderActiveBuzzBanner();
    scoreGrid.innerHTML = "";

    state.equipes.forEach(function (eq, i) {
      var isActive = gameState === "buzzed" && currentTeam === i;
      var isBanned = bannedTeams.indexOf(i) !== -1 && !isActive;

      var teamColor = primaryColor(eq);
      var card = document.createElement("div");
      card.className = "score-card anim-card" + (isActive ? " active" : "") + (isBanned ? " banned" : "");
      card.style.setProperty("--team-color", teamColor);

      var top = document.createElement("div");
      top.className = "top-row";
      var name = document.createElement("div");
      name.className = "name";
      name.textContent = eq.nom;
      top.appendChild(name);
      if (isBanned) {
        var tag = document.createElement("span");
        tag.className = "banned-tag";
        tag.textContent = "hors round";
        top.appendChild(tag);
      }
      card.appendChild(top);

      var swatchRow = document.createElement("div");
      swatchRow.className = "swatch-row";

      var hex = document.createElement("span");
      hex.className = "hex";
      hex.textContent = teamColor.toUpperCase() + " · " + couleurRoueLaPlusProche(teamColor);

      eq.couleursAffichage.forEach(function (c, ci) {
        var chip = document.createElement("span");
        chip.className = "affichage-chip";
        var sw = document.createElement("input");
        sw.type = "color";
        sw.className = "swatch swatch-input";
        sw.value = c;
        sw.title = "Couleur d'affichage " + (ci + 1) + " (écran animateur + écran public — n'affecte pas le DMX)";
        sw.addEventListener("input", function () {
          eq.couleursAffichage[ci] = sw.value;
          saveState();
          if (ci === 0) {
            var newPrimary = primaryColor(eq);
            hex.textContent = newPrimary.toUpperCase() + " · " + couleurRoueLaPlusProche(newPrimary);
            card.style.setProperty("--team-color", newPrimary);
          }
          if (publicOpen) renderPublicContent();
        });
        chip.appendChild(sw);
        if (eq.couleursAffichage.length > 1) {
          var rm = document.createElement("button");
          rm.className = "chip-remove";
          rm.textContent = "×";
          rm.title = "Retirer cette couleur";
          rm.addEventListener("click", function () {
            eq.couleursAffichage.splice(ci, 1);
            saveState();
            renderAnimateur();
          });
          chip.appendChild(rm);
        }
        swatchRow.appendChild(chip);
      });

      var addColorBtn = document.createElement("button");
      addColorBtn.className = "chip-add";
      addColorBtn.textContent = "+";
      addColorBtn.title = "Ajouter une couleur d'affichage (ex : plusieurs projecteurs de couleurs différentes)";
      addColorBtn.addEventListener("click", function () {
        eq.couleursAffichage.push(eq.couleursAffichage[eq.couleursAffichage.length - 1] || "#ffffff");
        saveState();
        renderAnimateur();
      });
      swatchRow.appendChild(addColorBtn);
      swatchRow.appendChild(hex);
      card.appendChild(swatchRow);

      var dmxColors = distinctProjectorColors(eq);
      if (dmxColors.length) {
        var dmxRow = document.createElement("div");
        dmxRow.className = "dmx-colors";
        dmxColors.forEach(function (c) {
          var chip = document.createElement("span");
          chip.className = "dmx-chip";
          chip.style.background = c;
          chip.title = "Couleur DMX réelle (projecteur) : " + c.toUpperCase();
          dmxRow.appendChild(chip);
        });
        card.appendChild(dmxRow);
      }

      var pointsLabel = document.createElement("div");
      pointsLabel.className = "points-label";
      pointsLabel.textContent = "Score";
      card.appendChild(pointsLabel);

      var points = document.createElement("div");
      points.className = "points mono";
      points.textContent = eq.score;
      card.appendChild(points);

      if (isActive) {
        var activeNote = document.createElement("div");
        activeNote.className = "active-note";
        activeNote.textContent = "En écoute — Valider / Refuser ci-dessus";
        card.appendChild(activeNote);
      } else {
        var buzzBtn = document.createElement("button");
        buzzBtn.className = "buzz-btn";
        buzzBtn.textContent = isBanned ? "Déjà passée" : "Buzz";
        buzzBtn.disabled = gameState !== "idle" || isBanned;
        buzzBtn.addEventListener("click", function () { handleBuzz(i); });
        card.appendChild(buzzBtn);

        var btns = document.createElement("div");
        btns.className = "point-btns";
        [1, 5, 10].forEach(function (val) {
          var b = document.createElement("button");
          b.textContent = "+" + val;
          b.addEventListener("click", function () {
            eq.score += val;
            saveState();
            renderAnimateur();
          });
          btns.appendChild(b);
        });
        card.appendChild(btns);
      }

      scoreGrid.appendChild(card);
    });
  }

  // ---------------- Podium ----------------
  function sortedTeams() {
    return state.equipes.map(function (eq, i) { return { eq: eq, i: i }; })
      .sort(function (a, b) { return b.eq.score - a.eq.score; });
  }

  function renderPodium() {
    var ranked = sortedTeams();
    var stage = document.getElementById("podium-stage");
    var rest = document.getElementById("podium-rest");
    var stats = document.getElementById("podium-stats");
    stage.innerHTML = "";
    rest.innerHTML = "";
    stats.innerHTML = "";

    var order = [1, 0, 2]; // 2e, 1er, 3e pour l'effet podium
    var medals = { 0: "1er", 1: "2e", 2: "3e" };
    order.forEach(function (rank) {
      var entry = ranked[rank];
      if (!entry) return;
      var slot = document.createElement("div");
      slot.className = "podium-slot p" + (rank + 1);
      var medal = document.createElement("div");
      medal.className = "medal";
      medal.textContent = medals[rank];
      var block = document.createElement("div");
      block.className = "block";
      var pname = document.createElement("div");
      pname.className = "pname";
      pname.textContent = entry.eq.nom;
      var pscore = document.createElement("div");
      pscore.className = "pscore";
      pscore.textContent = entry.eq.score + " pts";
      block.appendChild(pname);
      block.appendChild(pscore);
      slot.appendChild(medal);
      slot.appendChild(block);
      stage.appendChild(slot);
    });

    ranked.slice(3).forEach(function (entry, idx) {
      var row = document.createElement("div");
      row.className = "rest-row";
      var rank = document.createElement("span");
      rank.className = "rank mono";
      rank.textContent = (idx + 4) + ".";
      var label = document.createElement("span");
      label.textContent = entry.eq.nom;
      var score = document.createElement("span");
      score.className = "mono";
      score.textContent = entry.eq.score + " pts";
      row.appendChild(rank);
      row.appendChild(label);
      row.appendChild(score);
      rest.appendChild(row);
    });

    var total = state.equipes.reduce(function (s, eq) { return s + eq.score; }, 0);
    var avg = state.equipes.length ? (total / state.equipes.length) : 0;
    [
      { v: total, l: "Total" },
      { v: avg.toFixed(1), l: "Moyenne" },
      { v: state.equipes.length, l: "Équipes" }
    ].forEach(function (s) {
      var stat = document.createElement("div");
      stat.className = "stat";
      var v = document.createElement("div");
      v.className = "v mono";
      v.textContent = s.v;
      var l = document.createElement("div");
      l.className = "l";
      l.textContent = s.l;
      stat.appendChild(v);
      stat.appendChild(l);
      stats.appendChild(stat);
    });
  }

  // ---------------- Affichage public (projecteur) ----------------
  function appendPublicQuestion() {
    var q = currentQuestion();
    if (!q) return;
    var qDiv = document.createElement("div");
    qDiv.className = "p-question";
    qDiv.textContent = q.question;
    publicContent.appendChild(qDiv);
  }

  function renderPublicContent() {
    publicContent.innerHTML = "";
    if (gameState === "finished") {
      var ranked = sortedTeams();
      var title = document.createElement("div");
      title.className = "p-podium-title";
      title.textContent = "CLASSEMENT FINAL";
      publicContent.appendChild(title);
      ranked.slice(0, 3).forEach(function (entry, idx) {
        var line = document.createElement("div");
        line.className = "p-points";
        line.style.fontSize = idx === 0 ? "34px" : "22px";
        line.style.marginTop = "10px";
        line.textContent = (idx + 1) + ". " + entry.eq.nom + " — " + entry.eq.score + " pts";
        publicContent.appendChild(line);
      });
    } else if (gameState === "buzzed" && currentTeam !== null) {
      var eq = state.equipes[currentTeam];
      appendPublicQuestion();
      var ecoute = document.createElement("div");
      ecoute.className = "p-ecoute";
      ecoute.textContent = "EN ÉCOUTE";
      var team = document.createElement("div");
      team.className = "p-team";
      team.textContent = eq.nom;
      if (eq.couleursAffichage.length > 1) {
        team.style.backgroundImage = teamColorBackground(eq);
        team.classList.add("p-team-multi");
      } else {
        team.style.color = primaryColor(eq);
      }
      var qpts = state.pointsCourants;
      var pts = document.createElement("div");
      pts.className = "p-points";
      pts.textContent = qpts + (qpts > 1 ? " points" : " point");
      publicContent.appendChild(ecoute);
      publicContent.appendChild(team);
      publicContent.appendChild(pts);
      if (state.chrono.enabled) {
        var chrono = document.createElement("div");
        chrono.className = "p-chrono mono";
        var remaining = chronoDeadline !== null ? chronoDeadline - Date.now() : state.chrono.durationSec * 1000;
        chrono.textContent = formatChrono(remaining);
        chrono.style.color = remaining <= 3000 ? "#e0665f" : "#f4f4f6";
        publicContent.appendChild(chrono);
      }
    } else {
      appendPublicQuestion();
      var idle = document.createElement("div");
      idle.className = "p-idle";
      idle.textContent = "EN ATTENTE DE BUZZ";
      publicContent.appendChild(idle);
    }
  }

  function togglePublicOverlay() {
    publicOpen = !publicOpen;
    publicOverlay.hidden = !publicOpen;
    if (publicOpen) renderPublicContent();
  }

  document.getElementById("abb-valider").addEventListener("click", handleValider);
  document.getElementById("abb-refuser").addEventListener("click", handleRefuser);
  document.getElementById("btn-public").addEventListener("click", togglePublicOverlay);
  document.getElementById("btn-close-public").addEventListener("click", togglePublicOverlay);
  document.getElementById("btn-reset-game").addEventListener("click", resetGame);
  document.getElementById("btn-finish-game").addEventListener("click", finishGame);
  document.getElementById("btn-podium-reset").addEventListener("click", function () {
    resetGame();
    animLive.hidden = false;
    animPodium.hidden = true;
  });
  document.getElementById("btn-podium-continue").addEventListener("click", continueGame);

  document.getElementById("input-points-buzz").addEventListener("input", function (e) {
    state.pointsAuBuzz = parseInt(e.target.value, 10) || 0;
    saveState();
  });
  document.getElementById("chk-chrono-enabled").addEventListener("change", function (e) {
    state.chrono.enabled = e.target.checked;
    saveState();
    updateChronoReadout();
  });
  document.getElementById("input-chrono-duration").addEventListener("input", function (e) {
    var v = parseInt(e.target.value, 10) || 30;
    state.chrono.durationSec = Math.max(5, Math.min(300, v));
    saveState();
  });
  document.getElementById("chk-chrono-timeout").addEventListener("change", function (e) {
    state.chrono.timeoutFaux = e.target.checked;
    saveState();
  });

  document.getElementById("input-points-buzz").value = state.pointsAuBuzz;
  document.getElementById("chk-chrono-enabled").checked = state.chrono.enabled;
  document.getElementById("input-chrono-duration").value = state.chrono.durationSec;
  document.getElementById("chk-chrono-timeout").checked = state.chrono.timeoutFaux;

  renderFixtures();
  renderEquipesConfig();
  renderAnimateur();
  renderLog();
})();
