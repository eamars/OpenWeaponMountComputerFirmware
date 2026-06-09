# Cutover Policy Reference

Use this reference when a development plan changes existing behavior, removes legacy paths, migrates data, or needs an explicit rollout/cutover strategy.

## Strategy Definitions

Use these definitions verbatim or close to verbatim:

- **migration:** Move from old behavior to new behavior through explicit transitional steps. Temporary coexistence is allowed only where the plan says so. Data migration or backfill may be required. Old paths are removed after migration is verified.
- **compatible:** Preserve old and new behavior at the same time. Compatibility shims, adapters, fallback paths, dual reads/writes, or old API/state shapes are allowed only if explicitly listed.
- **bigbang:** Replace old behavior with new behavior in one cutover. No compatibility shims, no adapters, no fallback to old behavior, no dual path, and no preservation of old state/API shapes unless explicitly listed as retained.

## Policy Matrix

Use a table like this:

```md
## Cutover Policy

Overall strategy: bigbang

| Area | Policy | Instruction |
|---|---|---|
| RAG supervisor entrypoint | bigbang | Replace RAG1 with RAG2 directly. No fallback. |
| RAG state shape | bigbang | Remove `research_facts` and `research_metadata`. Do not preserve old state. |
| MongoDB legacy collections | migration | Drop through the approved migration path. Do not delete ad hoc. |
| Tests | bigbang | Delete obsolete RAG1 tests and create RAG2 replacement tests. |
```

If a local area policy conflicts with the overall strategy, the local area policy wins.

## Enforcement

Include enforcement language:

```md
## Cutover Policy Enforcement

- The responsible execution agent must follow the selected policy for each area.
- The agent must not choose a more conservative strategy by default.
- If an area is `bigbang`, delete or rewrite legacy references instead of preserving them.
- If an area is `migration`, follow the exact migration phases and cleanup gates listed in this plan.
- If an area is `compatible`, preserve only the compatibility surfaces explicitly listed in this plan.
- Any change to a cutover policy requires user approval before implementation.
```
