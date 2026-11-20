# Level Development Contract

Use this contract when a new U11 level needs cloud planning, generated assets, local implementation, review, and QA. It is a handoff format, not an authorization to merge, deploy, alter assets outside the stated output folder, or use another provider's tools.

## 1. Level Brief

The cloud planner creates one brief for one independently shippable objective:

```text
Level ID: L09
Objective: <one player-facing feature>
Campaign position: <START_STEP and transition>
Player experience: <short description>
Acceptance criteria: <observable outcomes>
Out of scope: <explicit exclusions>
Asset requests: <placeholder IDs or none>
Code tasks: <small, owned tasks and repository paths>
```

If a request contains separable objectives, create follow-up issues instead of placing them in one brief.

## 2. Placeholder IDs

The cloud planner owns placeholder creation. Code tasks may consume an ID but must not rename it.

```text
L<level>_<TYPE>_<NAME>
```

`TYPE` is one of `MODEL`, `TEXTURE`, `SFX`, `MUSIC`, or `WORLD`. Examples:

```text
L09_MODEL_TRINITY_GATE
L09_SFX_GATE_WAKE
L09_WORLD_CRYSTAL_MOON
```

The Level Brief records each placeholder. When an asset arrives, its asset-status record maps the same placeholder to the delivered relative path. Code continues to use the placeholder until the approved integration task updates the game's asset data.

## 3. Grok Asset Handoff

Each approved asset request includes the placeholder ID, required format, visual or audio requirements, and target level ID. Grok may write only beneath:

```text
\\HOME-SERVER\u11-assets\develop\<level-id>\
```

Grok must not delete or overwrite material outside that level directory. It reports delivery in `asset-status.json` in the same level directory:

```json
{
  "placeholderId": "L09_MODEL_TRINITY_GATE",
  "relativePath": "models/trinity_gate.glb",
  "type": "MODEL",
  "status": "ready",
  "timestamp": "2026-11-20T20:00:00Z"
}
```

`status` is `ready`, `blocked`, or `failed`. A blocked or failed asset does not authorize a substitute asset, rename, deletion, or code-scope expansion.

## 4. Parallel Local Code Tasks

The cloud implementation plan divides the brief into small tasks. Each task names its repository paths and one owner:

| Owner | Typical work | Rule |
| --- | --- | --- |
| Server1 local worker | level data, encounter definitions, game-state logic | Owns only its declared paths. |
| Home Server local worker | HUD, mission flow, presentation, or other declared code | Owns only its declared paths. |
| Grok asset worker | requested generated asset files | Writes only to its level folder under `develop`. |

Server1 and Home Server may run concurrently only when their declared repository paths do not overlap. Equal paths and parent/child paths conflict. The dispatcher stops ambiguous or overlapping work for Ryan; it does not guess, merge, retry, or reroute the task.

## 5. Review and Merge

Every code task produces a PR. Gemini review is a manual Antigravity handoff, not automated commenting. Its reviewer checks:

- The PR matches one approved Level Brief objective.
- Changed paths match the assigned task scope.
- Placeholder IDs are unchanged and asset-status entries use matching IDs.
- No asset files, asset sync rules, deployment scripts, or unrelated systems changed.
- The documented tablet-performance and gameplay constraints remain satisfied.

Ryan reviews the PR, records approval, and is the only merger. No provider may merge or enable auto-merge.

## 6. QA Handoff

After the relevant PR/build is available, ChatGPT creates a focused QA checklist from the Level Brief, PR diff, and delivered asset status. The checklist states setup, actions, expected results, performance checks, and evidence to collect.

Local workers execute only the listed checks with hidden PC windows where applicable and report results plus evidence. They do not deploy to the tablet, merge, or broaden the test scope. Ryan reviews the QA report before considering the feature complete.

## 7. Provider Boundaries

Every handoff names the provider and role: cloud planner, Grok asset worker, Server1 local implementer, Home Server local implementer, Gemini reviewer, or ChatGPT QA author.

This document is shared repository knowledge. It does not automatically grant any provider another provider's OAuth session, CLI, API key, remote control, skills, filesystem access, or approval authority. Provider-specific prompts and skills must be supplied explicitly.

## Standing Boundaries

- No direct push to `main`; all game-code changes use PRs.
- Assets stay out of Git and are never committed by code workers.
- Do not change `.gitignore`, `assets.manifest`, or `tools/assets-sync.ps1` without Ryan's explicit approval.
- Preserve the documented 85% edge throttle, Scarab art, no ship wiggle, single sun light, and tablet performance requirements.
- Do not deploy to the tablet or open visible PC test windows unless Ryan explicitly asks.
