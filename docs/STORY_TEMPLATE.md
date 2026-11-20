# Story Template (placeholder)

Replace the text and files; structure is what the engine reads. See LEVEL_FORMAT.md for every field.

## Premise (placeholder)
The Warden's fleet has crossed the Outer Rim. You are the last pilot of the U11 squadron. Chapter 1 takes you
from the Rim through the invasion grid and an asteroid field to the Warden's flagship.

## Cast
| id | portrait | role |
|---|---|---|
| PILOT | portraits/pilot.png | player character |
| COMMAND | portraits/command.png | mission control |
| THE WARDEN | portraits/warden.png | chapter boss (enemies/warden.json) |

## Chapter outline → campaign.json
| step | type | id | notes |
|---|---|---|---|
| 1 | level | level_01 (side) | intro cutscene `intro` |
| 2 | level | level_02 (top-down) | invaders grid |
| 3 | level | level_03 (side) | asteroid run |
| 4 | level | level_04 (top-down) | fortress approach |
| 5 | level | level_05 (side) | boss: warden; outro `chapter1_end` |

## Intro cutscene (shipped as assets/cutscenes/intro.json)
```json
{ "steps": [
  { "type": "background", "color": [10, 12, 40], "duration": 0 },
  { "type": "fade", "from_alpha": 1, "to_alpha": 0, "duration": 1.0 },
  { "type": "title", "text": "CHAPTER 1: THE RIM", "duration": 1.5 },
  { "type": "actor", "actor": "hero", "sprite": "sprites/red_fighter_pi3_sprites.json",
    "from": [-0.2, 0.45], "to": [0.3, 0.45], "duration": 2.0, "amount": 1.2 },
  { "type": "dialog", "speaker": "COMMAND", "portrait": "portraits/command.png",
    "text": "Pilot, the Warden's fleet has crossed the Outer Rim.\nPush through their lines and find the flagship." },
  { "type": "actor", "actor": "enemy", "sprite": "sprites/blue_racer_pi3_sprites.json",
    "from": [1.2, 0.3], "to": [0.75, 0.3], "duration": 1.5 },
  { "type": "dialog", "speaker": "PILOT", "portrait": "portraits/pilot.png", "text": "Contacts ahead. Weapons hot." },
  { "type": "sfx", "sfx": "laser" },
  { "type": "shake", "amount": 0.4, "duration": 0.4 },
  { "type": "fade", "from_alpha": 0, "to_alpha": 1, "duration": 0.6 }
] }
```

## Writing checklist
- Keep dialog lines ≤ ~60 chars and use `\n` (no automatic wrapping yet).
- Each new chapter: add a `chapters[]` entry; levels can be reused across chapters.
- Boss levels: set `"boss"`, give the boss 2–4 phases with rising aggression and minions in later phases.
- Alternate `mode` between levels for variety; invaders-style levels work best in top-down.
