---
name: development-plan
description: Use for Kazusa development plans: creating, reviewing, approving, executing, verifying, signing off, or handing off implementation, refactor, migration, decommission, prompt/LLM, database, or high-risk change plans.
---

# Development Plan

Create, review, execute, verify, and sign off development-plan work contracts
without inventing architecture, scope, contracts, or verification. Drafts may
contain questions; final and completed plans must not collect new scope.

## Planning Workflow

1. Inspect relevant docs, source, tests, current git state, and existing plans.
2. In this repo, read `development_plans/README.md` before touching plans.
3. Resolve decisions with the user during discovery; encode the confirmed
   decision as instruction.
4. Choose plan class: `small`, `medium`, `large`, or `high_risk_migration`.
5. Load `plan_contract.md` and `execution_gates.md` for final executable
   plans; load `cutover_policy.md` when behavior changes.
6. Before approval, reread the plan against this skill and loaded references.
7. When requested, run the optional independent plan review gate before
   approval, execution, or multi-stage handoff.
8. For executable plans, include a final independent code review gate.

## Execution Workflow

Execute only an `approved` or `in_progress` plan under `development_plans/active/`.
Load `execution_gates.md` before executing a plan. It is the canonical source
for parent/subagent ownership, test-contract-first order, verification,
evidence, review feedback, lifecycle updates, and final sign-off.

Normal execution requires native subagent capability. If subagents are
unavailable, stop unless the user explicitly requests fallback execution.

This skill does not depend on, supersede, or invoke any external planning or
execution framework. If another framework is available, use it only when the
user explicitly asks for that framework.

## References

| Read | When |
|---|---|
| `references/plan_contract.md` | Always for final executable plans. |
| `references/cutover_policy.md` | Behavior changes, legacy removal, data migration, or rollout strategy. |
| `references/execution_gates.md` | Always for final executable plans and before executing a plan; covers execution model, steps, verification, evidence, handoff, and review gates. |

## Local Registry Rule

`development_plans/README.md` owns lifecycle policy. Execute only `approved`
or `in_progress` plans under `active/`; use the registry for all other folders.

## Core Rules

- Serve two audiences: human owner first, parent/execution agents second.
- Treat final plans as work contracts with fixed scope, contracts,
  verification, and acceptance criteria.
- Name mandatory skills explicitly and copy critical skill-derived rules into
  the plan because context may compact.
- Use `plan_contract.md` as the source for required sections and final-plan
  prohibitions.
- Use `execution_gates.md` as the source for parent-led native subagent
  execution, focused test contract, verification, evidence, review, and
  sign-off.
- Do not authorize new architecture, compatibility layers, fallback paths,
  helper wrappers, extra features, or unrelated cleanup unless explicitly in
  scope and justified.

## Final-Plan Prohibitions

Final plans must not contain unresolved questions or decision points. Avoid
`TBD`, `maybe`, `consider`, `choose one`, `option A / option B`, and open-ended
recommendations. Assumptions must be fixed operating inputs, not disguised
questions.

## Style

Use direct instructions, stable names and paths, explicit scope boundaries, and
exact verification gates. Avoid hidden decisions, ambiguous safety language,
stale line-number-only references, and unaccepted recommendations.
