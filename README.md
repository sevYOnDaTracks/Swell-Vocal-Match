# Swell Vocal Match — 0.1.0 prototype

EQ et compression 3 bandes pour la voix, avec profils, analyse automatique, réduction
dynamique des résonances et de-esser. VST3, AU et Standalone, Windows et Mac universel.

## Utilisation

1. Choisir le **type de voix** (Grave, Médium, Aiguë) et un **profil**.
2. Cliquer sur **Analyser ma voix** puis lire la piste voix : le plugin écoute 12 secondes
   de passages avec du son, puis propose une correction et repère jusqu'à 3 résonances.
3. Doser avec **Intensité** (50 % = réglages du profil). **Résonances** règle la réduction
   des marqueurs, **De-esser** les sifflantes, **Sortie** le niveau final.
4. Cliquer sur **Corps**, **Présence** ou **Air** pour écouter une zone seule ; **Écouter le retiré**
   fait entendre ce que la réduction des résonances enlève. Un clic sur un marqueur le coupe.
5. **A/B** garde deux jeux de réglages complets. **Volume auto** garde le niveau perçu stable.
   **Réglages avancés** donne une correction manuelle par bande.

| Profil | Direction |
|---|---|
| Late Night | Posé sur les basses, compression légère |
| Neon Auto | Dense, pour l'Auto-Tune |
| Diva Power | Puissant et en avant |
| Sharp Flow | Net et très présent |

Les profils sont des points de départ inspirés de recherches sur des mix de référence,
à valider à l'écoute. Aucun nom d'artiste n'est utilisé dans le produit.

## Traitement

- Passe-haut selon le type de voix, EQ 4 bandes (Corps, Bas-médium, Présence, Air).
- Jusqu'à 3 coupes étroites dynamiques : elles se creusent quand la résonance ressort
  plus que d'habitude.
- Compression 3 bandes (crossovers Linkwitz-Riley 400 Hz et 5 kHz), seuil qui suit les
  passages forts de chaque bande, réduction limitée à 2,5 fois la valeur du profil.
- De-esser sur la bande Air, volume automatique lent, bypass sans clic.
- Aucune latence : utilisable pendant l'enregistrement.

## Limites connues

- L'analyse compare la voix à une courbe cible provisoire, calculée sur 3 voix seulement.
- Le volume automatique met 3 à 4 secondes à se stabiliser après un changement.
- Ce n'est pas un limiteur : garder de la marge en sortie.
- Mac : signé en ad hoc, non notarisé. Windows : pas encore testé sur une vraie machine.

## Compiler

Mac : `./build-mac.sh` (universel arm64 + x86_64, tests inclus).
Windows : Visual Studio 2022 Build Tools C++, CMake et Git ; lancer `build.ps1`.
La CI GitHub compile Windows et Mac à chaque envoi.
