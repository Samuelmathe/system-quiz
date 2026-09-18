// ==========================================================================
// BOÎTIER BUZZER ÉQUIPE — paramétrique, 2 variantes (Nano+nRF24 / ESP32)
// Architecture 2 étages démontables : étage PCB (modules sur connecteurs
// femelles) en haut avec le bouton, étage batterie en dessous avec sa
// propre trappe — accès à la batterie sans démonter l'électronique.
// ==========================================================================
// Basé sur docs/BOM_BUZZER_EQUIPE.md. Dimensions ESP32 (51,45 x 23,37mm,
// pas 2,54mm, entraxe rangées 22,86mm) vérifiées par recherche web pour un
// DOIT ESP32 DevKit V1 30 broches typique -- À REMESURER sur la carte
// réellement reçue (les clones varient). Dimensions Nano/nRF24/batterie
// toujours approximatives (voir BOM), à ajuster aussi.
//
// Modules montés sur connecteurs femelles (pas soudés directement) : la
// hauteur "hauteur_headers" ci-dessous doit couvrir header femelle +
// épaisseur du module + ses propres composants en hauteur.
//
// Je n'ai pas d'accès root dans cet environnement pour garder OpenSCAD
// installé en permanence : ce fichier a été rendu et vérifié (géométrie
// valide, "Simple: yes") au moment de l'écrire, mais RELISEZ-le et
// ouvrez-le vous-même dans OpenSCAD avant impression -- ne faites pas
// confiance au fichier les yeux fermés.
//
// Utilisation :
//   1. Ouvrez ce fichier dans OpenSCAD.
//   2. Choisissez la variante avec VARIANTE ci-dessous ("nano" ou "esp32").
//   3. F5 pour prévisualiser, F6 pour le rendu final, exportez en .stl.
// ==========================================================================

// ---- CHOIX DE LA VARIANTE ----
VARIANTE = "nano"; // "nano" ou "esp32"

// ---- PARAMÈTRES GÉNÉRAUX (communs aux deux variantes) ----
epaisseur_paroi  = 2.0;   // épaisseur des parois imprimées
jeu              = 0.5;   // jeu (clearance) autour des composants, par côté
hauteur_headers  = 14;    // au-dessus du plancher PCB : header femelle + module + ses composants
hauteur_shelf    = 1.6;   // épaisseur de l'étagère qui sépare étage PCB / étage batterie

// Empreinte de l'étage PCB (modules + connecteurs, côte à côte) -- voir
// note de dimensions ESP32 ci-dessus pour la variante "esp32"
pcb_largeur_nano   = 38;  // Nano (~18mm large) + nRF24 (~15mm) côte à côte + marge
pcb_longueur_nano  = 48;  // le plus long des deux modules (Nano ~43mm) + marge
// ESP32-DevKitC-32 (CH340C, USB-C) : dimensions CONFIRMEES par la fiche
// produit du modele envisage cote equipe -- 52 x 28mm, ~9.5g. Marge de
// degagement ajoutee pour le connecteur/les pattes de header.
pcb_largeur_esp32  = 34;  // 28mm de large + degagement
pcb_longueur_esp32 = 58;  // 52mm de long + marge

// Batterie LiPo (approximatif — À REMESURER sur la cellule réellement achetée)
batterie_nano_l   = 30;   // 500-600 mAh, typique
batterie_nano_p   = 25;
batterie_nano_h   = 7;
batterie_esp32_l  = 50;   // 1000-1200 mAh, typique — bien plus grande
batterie_esp32_p  = 34;
batterie_esp32_h  = 7;

// Module charge+protection+boost 3-en-1 ("Type-C USB 5V Step-Up Booster
// Lithium Battery Charging and Protection Module for Power Bank", ex.
// réf. 134N3P) — loge DANS L'ÉTAGE BATTERIE, à côté de la cellule (câblage
// BAT+/BAT- en parallèle sur la batterie, sortie 5V/GND remonte par la
// fente de passage de câbles jusqu'à J2 sur le PCB). Placé côte à côte
// avec la batterie le long de la largeur -- dimensions estimées
// génériques pour ce type de module, À REMESURER sur le module réellement
// reçu (elles varient pas mal d'un vendeur à l'autre).
module_charge_l = 26;   // longueur (mm)
module_charge_p = 18;   // largeur (mm)
module_charge_h = 6;    // épaisseur, composants compris (mm)

// Bouton "BUZZ" — type arcade, gros et satisfaisant à presser (pas un petit
// bouton tactile 6mm) : diamètre de perçage à ajuster selon le bouton choisi
bouton_diametre  = 24;

// Port USB-C du module de charge (cutout sur l'étage batterie)
usb_c_largeur    = 9.5;
usb_c_hauteur    = 3.5;

// Interrupteur (glissière) — cutout sur l'étage PCB (facile d'accès en haut)
interrupteur_largeur = 12;
interrupteur_hauteur = 6;

// Fente de passage de fils entre étage batterie et étage PCB (alimentation
// + cables du module de charge)
fente_cables_largeur = 10;
fente_cables_hauteur = 3;

// Vis (auto-taraudeuses dans bossages imprimés) -- 3 jeux : couvercle sup.
// -> corps, et corps -> trappe batterie
vis_diametre_ame = 2.4;   // âme pour vis M3 autotaraudeuse
bossage_diametre = 7;
marge_coin       = 6;     // distance des bossages par rapport aux coins

// ---- DIMENSIONS DÉRIVÉES SELON LA VARIANTE ----
pcb_largeur  = (VARIANTE == "esp32") ? pcb_largeur_esp32  : pcb_largeur_nano;
pcb_longueur = (VARIANTE == "esp32") ? pcb_longueur_esp32 : pcb_longueur_nano;
bat_l        = (VARIANTE == "esp32") ? batterie_esp32_l   : batterie_nano_l;
bat_p        = (VARIANTE == "esp32") ? batterie_esp32_p   : batterie_nano_p;
bat_h        = (VARIANTE == "esp32") ? batterie_esp32_h   : batterie_nano_h;

// Empreinte de l'étage batterie = batterie + module charge côte à côte
// (le long de la largeur), pas juste la batterie seule -- sinon le module
// ne rentre pas à côté d'elle dans la trappe du bas.
etage_bat_largeur  = bat_p + module_charge_p + jeu;
etage_bat_longueur = max(bat_l, module_charge_l);
etage_bat_hauteur  = max(bat_h, module_charge_h);

// Étages empilés (pas côte à côte) -> l'empreinte extérieure commune est
// la plus grande des deux compartiments, dans chaque dimension.
interieur_largeur  = max(pcb_largeur, etage_bat_largeur) + 2*jeu;
interieur_longueur = max(pcb_longueur, etage_bat_longueur) + 2*jeu;
exterieur_largeur  = interieur_largeur  + 2*epaisseur_paroi;
exterieur_longueur = interieur_longueur + 2*epaisseur_paroi;

hauteur_etage_pcb      = hauteur_headers;
hauteur_etage_batterie = etage_bat_hauteur + 2*jeu + 2; // +2mm de marge de manoeuvre

$fn = 48; // résolution des cercles/cylindres

// ---- BOÎTE ARRONDIE (helper) ----
module boite_arrondie(l, p, h, rayon) {
    hull() {
        for (x = [rayon, l - rayon])
            for (y = [rayon, p - rayon])
                translate([x, y, 0])
                    cylinder(h = h, r = rayon);
    }
}

// ---- POSITIONS DES BOSSAGES DE VIS (4 coins) ----
function pos_bossages() = [
    [marge_coin, marge_coin],
    [exterieur_largeur - marge_coin, marge_coin],
    [marge_coin, exterieur_longueur - marge_coin],
    [exterieur_largeur - marge_coin, exterieur_longueur - marge_coin]
];

// ==========================================================================
// CORPS PRINCIPAL : étage PCB (haut, fermé) + étagère + étage batterie
// (bas, OUVERT -- ferme par la trappe séparée). Un seul corps imprimé,
// mais deux volumes internes accessibles indépendamment : couvercle
// supérieur pour le PCB, trappe du dessous pour la batterie.
// ==========================================================================
module corps_principal() {
    hauteur_totale = hauteur_etage_pcb + hauteur_shelf + hauteur_etage_batterie;

    difference() {
        union() {
            boite_arrondie(exterieur_largeur, exterieur_longueur, hauteur_totale, 4);

            // Bossages de vis (pleins sur toute la hauteur, perces plus bas)
            for (pos = pos_bossages())
                translate([pos[0], pos[1], 0])
                    cylinder(h = hauteur_totale, d = bossage_diametre);
        }

        // Cavité étage batterie (en bas, ouverte vers le dessous)
        translate([epaisseur_paroi, epaisseur_paroi, -1])
            cube([interieur_largeur, interieur_longueur, hauteur_etage_batterie + 1]);

        // Cavité étage PCB (au-dessus de l'étagère, ouverte vers le dessus
        // -- fermée par le couvercle supérieur)
        translate([epaisseur_paroi, epaisseur_paroi, hauteur_etage_batterie + hauteur_shelf])
            cube([interieur_largeur, interieur_longueur, hauteur_etage_pcb + 1]);

        // Fente de passage des câbles à travers l'étagère
        translate([exterieur_largeur/2 - fente_cables_largeur/2,
                   epaisseur_paroi - 0.5,
                   hauteur_etage_batterie])
            cube([fente_cables_largeur, fente_cables_hauteur, hauteur_shelf + 1]);

        // Avant-trous vis (traversant tout le corps, tête côté couvercle sup.,
        // écrou/auto-taraudage côté trappe -- simplifié en trou simple ici)
        for (pos = pos_bossages())
            translate([pos[0], pos[1], -1])
                cylinder(h = hauteur_totale + 2, d = vis_diametre_ame);

        // Découpe USB-C sur l'étage batterie (accès au port de charge)
        translate([exterieur_largeur/2 - usb_c_largeur/2,
                   -1,
                   hauteur_etage_batterie/2 - usb_c_hauteur/2 + epaisseur_paroi/2])
            cube([usb_c_largeur, epaisseur_paroi + 2, usb_c_hauteur]);

        // Découpe interrupteur sur l'étage PCB (facile d'accès, près du haut)
        translate([-1,
                   marge_coin*2,
                   hauteur_etage_batterie + hauteur_shelf + hauteur_etage_pcb/2 - interrupteur_hauteur/2])
            cube([epaisseur_paroi + 2, interrupteur_largeur, interrupteur_hauteur]);
    }
}

// ==========================================================================
// COUVERCLE SUPÉRIEUR (étage PCB) -- avec le trou du bouton BUZZ
// ==========================================================================
module couvercle_superieur() {
    epaisseur_couvercle = epaisseur_paroi + 1;
    difference() {
        union() {
            boite_arrondie(exterieur_largeur, exterieur_longueur, epaisseur_couvercle, 4);
            for (pos = pos_bossages())
                translate([pos[0], pos[1], 0])
                    cylinder(h = epaisseur_couvercle, d = bossage_diametre + 2*jeu + 1);
        }
        for (pos = pos_bossages())
            translate([pos[0], pos[1], -1])
                cylinder(h = epaisseur_couvercle + 2, d = vis_diametre_ame + 1);

        translate([exterieur_largeur/2, exterieur_longueur/2, -1])
            cylinder(h = epaisseur_couvercle + 2, d = bouton_diametre);
    }
}

// ==========================================================================
// TRAPPE BATTERIE (étage du bas) -- démontable indépendamment du couvercle
// supérieur, pour changer/recharger la batterie sans toucher au PCB.
// ==========================================================================
module trappe_batterie() {
    epaisseur_trappe = epaisseur_paroi + 1;
    difference() {
        union() {
            boite_arrondie(exterieur_largeur, exterieur_longueur, epaisseur_trappe, 4);
            for (pos = pos_bossages())
                translate([pos[0], pos[1], 0])
                    cylinder(h = epaisseur_trappe, d = bossage_diametre + 2*jeu + 1);
        }
        for (pos = pos_bossages())
            translate([pos[0], pos[1], -1])
                cylinder(h = epaisseur_trappe + 2, d = vis_diametre_ame + 1);
    }
}

// ==========================================================================
// RENDU (les 3 pièces décalées côte à côte pour visualiser à l'impression)
// ==========================================================================
corps_principal();
translate([exterieur_largeur + 15, 0, 0])
    couvercle_superieur();
translate([2*(exterieur_largeur + 15), 0, 0])
    trappe_batterie();

echo(str("Variante : ", VARIANTE));
echo(str("Empreinte exterieure (L x P) : ", exterieur_largeur, " x ", exterieur_longueur, " mm"));
echo(str("Hauteur totale (corps + couvercle + trappe) : ",
    hauteur_etage_batterie + hauteur_shelf + hauteur_etage_pcb + 2*(epaisseur_paroi + 1), " mm"));
