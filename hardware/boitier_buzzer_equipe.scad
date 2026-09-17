// ==========================================================================
// BOÎTIER BUZZER ÉQUIPE — paramétrique, 2 variantes (Nano+nRF24 / ESP32)
// ==========================================================================
// Basé sur docs/BOM_BUZZER_EQUIPE.md. TOUTES les dimensions ci-dessous sont
// des valeurs TYPIQUES de composants courants, PAS des mesures faites sur
// votre matériel réel — mesurez vos modules exacts (pied à coulisse) et
// ajustez les variables avant impression. Je n'ai pas pu faire rendre ce
// fichier par OpenSCAD dans cet environnement (pas d'accès root pour
// l'installer) : relisez/ouvrez-le dans OpenSCAD avant de lancer une
// impression, ne faites pas confiance au fichier les yeux fermés.
//
// Utilisation :
//   1. Ouvrez ce fichier dans OpenSCAD.
//   2. Choisissez la variante avec VARIANTE ci-dessous ("nano" ou "esp32").
//   3. F5 pour prévisualiser, F6 pour le rendu final, exportez en .stl.
// ==========================================================================

// ---- CHOIX DE LA VARIANTE ----
VARIANTE = "nano"; // "nano" ou "esp32"

// ---- PARAMÈTRES GÉNÉRAUX (communs aux deux variantes) ----
epaisseur_paroi   = 2.0;   // épaisseur des parois imprimées
jeu               = 0.3;   // jeu (clearance) autour des composants, par côté
hauteur_interieure = 14;   // hauteur utile intérieure (au-dessus du PCB) pour loger batterie + composants empilés

// Carte électronique (approximatif — voir docs/BOM_BUZZER_EQUIPE.md)
pcb_largeur_nano  = 20;    // Nano ~18mm + marge de découpe PCB
pcb_longueur_nano = 45;    // Nano ~43mm + marge
pcb_largeur_esp32 = 27;    // ESP32 Dev Module ~25mm + marge
pcb_longueur_esp32 = 50;   // ESP32 Dev Module ~48mm + marge

// Batterie LiPo (approximatif — À REMESURER sur la cellule réellement achetée)
batterie_nano_l   = 30;    // 500-600 mAh, typique
batterie_nano_p   = 25;
batterie_nano_h   = 7;
batterie_esp32_l  = 50;    // 1000-1200 mAh, typique — bien plus grande
batterie_esp32_p  = 34;
batterie_esp32_h  = 7;

// Bouton "BUZZ" — type arcade, gros et satisfaisant à presser (pas un petit
// bouton tactile 6mm) : diamètre de perçage à ajuster selon le bouton choisi
bouton_diametre   = 24;

// Port USB-C du module de charge (cutout sur le côté)
usb_c_largeur     = 9.5;
usb_c_hauteur     = 3.5;

// Interrupteur (glissière) — cutout rectangulaire sur le côté
interrupteur_largeur = 12;
interrupteur_hauteur = 6;

// Vis de fermeture du couvercle (auto-taraudeuses dans bossages imprimés)
vis_diametre_ame  = 2.4;   // diamètre d'âme pour vis M3 autotaraudeuse dans plastique
bossage_diametre  = 7;
marge_coin        = 6;     // distance des bossages par rapport aux coins

// ---- DIMENSIONS DÉRIVÉES SELON LA VARIANTE ----
pcb_largeur  = (VARIANTE == "esp32") ? pcb_largeur_esp32  : pcb_largeur_nano;
pcb_longueur = (VARIANTE == "esp32") ? pcb_longueur_esp32 : pcb_longueur_nano;
bat_l        = (VARIANTE == "esp32") ? batterie_esp32_l   : batterie_nano_l;
bat_p        = (VARIANTE == "esp32") ? batterie_esp32_p   : batterie_nano_p;
bat_h        = (VARIANTE == "esp32") ? batterie_esp32_h   : batterie_nano_h;

// [FIX] Le PCB et la batterie côte à côte SUR LA LONGUEUR donnaient un
// boîtier en forme de baguette (~30 x 85mm) bien trop étroit pour un bouton
// arcade confortable -- le trou de bouton dépassait presque la largeur du
// couvercle. Disposition changée : côte à côte SUR LA LARGEUR, ce qui donne
// un gabarit plus carré, plus naturel à tenir en main et à presser au pouce.
espace_cablage = 5;
interieur_largeur  = pcb_largeur + bat_p + espace_cablage + 2*jeu;
interieur_longueur = max(pcb_longueur, bat_l) + 2*jeu;
interieur_hauteur  = max(hauteur_interieure, bat_h + 6); // +6 pour le PCB + composants en hauteur

exterieur_largeur  = interieur_largeur  + 2*epaisseur_paroi;
exterieur_longueur = interieur_longueur + 2*epaisseur_paroi;
hauteur_base       = interieur_hauteur * 0.65;  // le bac principal
hauteur_couvercle  = interieur_hauteur * 0.35 + epaisseur_paroi;

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
// BAC PRINCIPAL (base)
// ==========================================================================
module base() {
    difference() {
        union() {
            // Coque extérieure
            boite_arrondie(exterieur_largeur, exterieur_longueur, hauteur_base, 4);

            // Bossages de vis (pleins, perces plus bas)
            for (pos = pos_bossages())
                translate([pos[0], pos[1], 0])
                    cylinder(h = hauteur_base, d = bossage_diametre);
        }

        // Évidement intérieur (creuse la coque)
        translate([epaisseur_paroi, epaisseur_paroi, epaisseur_paroi])
            cube([interieur_largeur, interieur_longueur, hauteur_base]);

        // Avant-trou pour vis autotaraudeuses dans les bossages
        for (pos = pos_bossages())
            translate([pos[0], pos[1], -1])
                cylinder(h = hauteur_base + 2, d = vis_diametre_ame);

        // Découpe port USB-C : sur le petit côté (y=0), centrée en X sur la
        // zone batterie/module de charge (moitié droite du boîtier, voir
        // disposition PCB | espace câblage | batterie ci-dessus)
        translate([epaisseur_paroi + pcb_largeur + espace_cablage + bat_p/2 - usb_c_largeur/2,
                   -1,
                   epaisseur_paroi + 2])
            cube([usb_c_largeur, epaisseur_paroi + 2, usb_c_hauteur]);

        // Découpe interrupteur, sur le grand côté (x=0), pres du coin
        translate([-1,
                   marge_coin*2,
                   epaisseur_paroi + 2])
            cube([epaisseur_paroi + 2, interrupteur_largeur, interrupteur_hauteur]);
    }
}

// ==========================================================================
// COUVERCLE (avec le trou du bouton BUZZ)
// ==========================================================================
module couvercle() {
    difference() {
        union() {
            boite_arrondie(exterieur_largeur, exterieur_longueur, hauteur_couvercle, 4);

            for (pos = pos_bossages())
                translate([pos[0], pos[1], 0])
                    cylinder(h = hauteur_couvercle, d = bossage_diametre + 2 * jeu + 1);
        }

        // Trou pour vis (traversant, tête fraisée simplifiée en cylindre simple)
        for (pos = pos_bossages())
            translate([pos[0], pos[1], -1])
                cylinder(h = hauteur_couvercle + 2, d = vis_diametre_ame + 1);

        // [FIX] Centrer le trou sur la moitié PCB le faisait toucher/déborder
        // le bord exterieur du couvercle (bouton plus large que la moitié
        // PCB elle-même) -> bord du cylindre tangent au contour arrondi,
        // geometrie non-manifold (CGAL "Simple: no"). Un bouton panel-mount
        // se cable de toute facon par 2 fils volants jusqu'au PCB, donc rien
        // n'oblige a le centrer pile au-dessus de la carte : centré sur tout
        // le couvercle, avec marge de chaque côté.
        translate([exterieur_largeur/2, exterieur_longueur/2, -1])
            cylinder(h = hauteur_couvercle + 2, d = bouton_diametre);
    }
}

// ==========================================================================
// RENDU (base + couvercle décalé à côté pour visualiser les deux à l'impression)
// ==========================================================================
base();
translate([exterieur_largeur + 15, 0, 0])
    couvercle();

echo(str("Variante : ", VARIANTE));
echo(str("Boîtier extérieur (L x P x H totale) : ",
    exterieur_largeur, " x ", exterieur_longueur, " x ", hauteur_base + hauteur_couvercle, " mm"));
