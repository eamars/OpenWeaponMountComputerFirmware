# Plan Contract Reference

Read this reference when writing or reviewing a final executable development
plan. Keep discovery notes, unresolved questions, and options out of final-plan
sections.

## Table Of Contents

- [Plan Lifecycle](#plan-lifecycle)
- [Filename Rule](#filename-rule)
- [Required Top Matter](#required-top-matter)
- [Length Budget](#length-budget)
- [Mandatory Sections](#mandatory-sections)
- [Mandatory Skills](#mandatory-skills)
- [Mandatory Rules](#mandatory-rules)
- [No Unresolved Questions](#no-unresolved-questions)
- [Must Do](#must-do)
- [Deferred](#deferred)
- [Overdesign Guardrail](#overdesign-guardrail)
- [Agent Autonomy Boundaries](#agent-autonomy-boundaries)
- [Context](#context)
- [Target State](#target-state)
- [Design Decisions](#design-decisions)
- [Contracts And Data Shapes](#contracts-and-data-shapes)
- [LLM Call And Context Budget](#llm-call-and-context-budget)
- [Change Surface](#change-surface)
- [Execution Model](#execution-model)
- [Independent Plan Review](#independent-plan-review)
- [Independent Code Review](#independent-code-review)
- [Acceptance Criteria](#acceptance-criteria)
- [Risks](#risks)

## Plan Lifecycle

Keep these stages separate:

- **Discovery / Drafting:** questions, options, code inspection, tradeoffs, and
  user confirmation are allowed here.
- **Final Development Plan:** no unresolved questions, no decision prompts, no
  alternatives left for parent or execution agents.
- **Execution Record:** what was done, what passed, what failed, and evidence.

Do not mix final-plan content with unresolved design discussion.

Treat completed plans as closed historical records. After a plan reaches
`completed`, do not add newly requested work, follow-up ideas, expanded scope,
or changed requirements to that completed plan. New work belongs in a new plan,
a superseding plan, or a separate follow-up plan with its own status and
checklist.

## Filename Rule

Development plan filenames must be all lowercase. Prefer snake_case:

```text
rag1_decommission_plan.md
cache2_invalidation_migration_plan.md
```

Do not use uppercase letters in plan filenames:

```text
RAG1_decommission_plan.md
RAG_SUPERVISOR2_PLAN.md
```

## Required Top Matter

Start each plan with a compact human-readable summary:

```md
# <lowercase plan title>

## Summary

- Goal:
- Plan class:
- Status:
- Mandatory skills:
- Overall cutover strategy:
- Highest-risk areas:
- Acceptance criteria:
```

Use one of these status values:

```text
draft | approved | in_progress | completed | superseded
```

Use one of these plan classes:

```text
small | medium | large | high_risk_migration
```

## Length Budget

Choose the plan class before writing.

| Plan class | Typical scope | Target length | Maximum length |
|---|---|---:|---:|
| small | 1-2 files, low risk | 80-150 lines | 200 lines |
| medium | several files, no data migration | 150-300 lines | 400 lines |
| large | many files, contracts/prompts/tests | 300-600 lines | 800 lines |
| high_risk_migration | deletion, DB work, production behavior change | 500-900 lines | 1200 lines |

If a plan exceeds the target, compress repetition, move examples to appendices,
or reference existing docs. Do not remove mandatory rules, scope, contracts,
cutover policy, implementation order, or verification gates just to meet the
length budget.

## Mandatory Sections

Every final plan must include these sections:

```md
## Summary
## Context
## Mandatory Skills
## Mandatory Rules
## Must Do
## Deferred
## Cutover Policy
## Target State
## Design Decisions
## Change Surface
## Overdesign Guardrail
## Agent Autonomy Boundaries
## Implementation Order
## Execution Model
## Progress Checklist
## Verification
## Independent Code Review
## Acceptance Criteria
```

Add these sections whenever relevant:

```md
## Data Migration
## Contracts And Data Shapes
## LLM Call And Context Budget
## Independent Plan Review
## Execution Evidence
## Risks
```

Use the mandatory-section order above as the canonical order for final plans.
Reference sections below are grouped for explanation and may not appear in the
same order.

## Mandatory Skills

Each plan must explicitly name every skill the parent agent, production-code
subagent, review subagent, or fallback execution agent is required to load
before making changes or reviewing work. Do not rely on agents inferring
mandatory skills from the repo, task, memories, or surrounding conversation.

Use a short, concrete list:

```md
## Mandatory Skills

- `py-style`: load before editing Python files.
- `test-style-and-execution`: load before adding, changing, or running tests.
- `local-llm-architecture`: load before changing prompt, graph, RAG, memory,
  cognition, dialog, evaluator, or background LLM behavior.
```

If no specialized skill is required, state that explicitly:

```md
## Mandatory Skills

- No specialized skill is required for this plan.
```

For plans that touch multiple domains, list the skills in the order the agent
should load them and state which stage each skill governs. The plan must also
copy the critical skill-derived rules into `Mandatory Rules`; naming a skill is
not enough because execution agents may lose context after compaction.

## Mandatory Rules

Each plan must explicitly state the project-specific rules the agent must
follow. Do not rely on the agent loading skills, memories, or repo conventions
efficiently.

Include rules such as:

- coding style rules
- test execution rules
- plan reread rules after automatic context compaction and major checklist
  sign-off
- prompt safety rules
- trusted vs untrusted prompt boundaries
- LLM call and context budget rules for prompt, agent, RAG, cognition, dialog,
  evaluator, or background LLM changes
- database safety rules
- forbidden filtering or validation patterns
- target-module boundary rules for changes outside the primary module
- required skill-derived practices, copied into the plan

Write the rules directly in the plan. It is acceptable and often desirable to
duplicate important skill content here.

Every final plan must include these plan-continuity rules in `Mandatory Rules`:

- After any automatic context compaction, the parent or active execution agent
  must reread this entire plan before continuing implementation, verification,
  handoff, or final reporting.
- After signing off any major progress checklist stage, the parent or active
  execution agent must reread this entire plan before starting the next stage.
- Before final completion, lifecycle status changes, merge, or sign-off, the
  parent agent must run the plan's `Independent Code Review` gate and record
  the result in `Execution Evidence`.
- The plan's `Execution Model` must use parent-led native subagent execution
  unless the user explicitly approves fallback execution.

## No Unresolved Questions

Final plans must not contain unresolved questions or decision points.

Avoid these in final plans:

- `TBD`
- `maybe`
- `consider`
- `choose one`
- `option A / option B`
- `ask the user whether...`
- open-ended recommendations

Resolve uncertainty before finalizing the plan. If a decision depends on code
inspection, inspect the code first. If a decision depends on user preference,
ask the user during discovery, then encode the confirmed decision as an
instruction.

Assumptions are allowed, but they must be fixed operating inputs:

```md
## Assumptions

- RAG2 is the only supported retrieval path after this refactor.
- Compatibility with the RAG1 state shape is intentionally not preserved.
- Legacy MongoDB collections may be dropped through the approved migration path.
```

Do not write assumptions as disguised questions.

For new modules, an unapproved or undefined public interface counts as an
unresolved question. Do not finalize the plan until the module boundary and
interface are accepted by the user.

## Must Do

The `Must Do` section defines non-negotiable scope. Use directive language:

```md
## Must Do

- Replace all `research_facts` consumers with `rag_result`.
- Delete RAG1 modules listed in this plan.
- Add projection and invalidation tests listed in this plan.
- Run every verification command in the Verification section.
```

The responsible execution agent must not downgrade, reinterpret, or skip these
items.

## Deferred

The `Deferred` section defines explicit non-scope. Use directive language:

```md
## Deferred

- Do not redesign RAG2 helper-agent routing.
- Do not add persistent Cache2 storage.
- Do not create compatibility shims for the old RAG1 state shape.
- Do not refactor unrelated prompt architecture.
```

The responsible execution agent must not opportunistically do deferred work,
even if it looks useful.

## Overdesign Guardrail

Every final executable plan must include an `Overdesign Guardrail` section
before `Agent Autonomy Boundaries`. This section prevents the plan from
turning a narrow requirement into speculative architecture.

Use this shape:

```md
## Overdesign Guardrail

- Actual problem: <one sentence describing the observable bug, missing
  behavior, risk, or user need being fixed now.>
- Minimal change: <the smallest viable contract/code/prompt/schema change that
  satisfies the actual problem.>
- Ownership boundaries: <which layer owns each decision, such as LLM semantic
  judgment, deterministic validation, persistence, scheduling, or adapter
  delivery.>
- Rejected complexity: <specific fields, helpers, modes, fallback paths,
  compatibility layers, extra agents, retries, abstractions, or integrations
  that are intentionally not being added.>
- Evidence threshold: <what observed failure, test, measurement, or approved
  near-term integration would be required before adding the rejected
  complexity later.>
```

The guardrail must reject any new dimension of flexibility unless it is needed
by the current requirement, fixes a current failure, or supports an approved
near-term integration point. If a plan adds a new field, helper, layer, flag,
mode, fallback path, or abstraction, the guardrail must explain why
deterministic existing code or a smaller local change cannot handle the need.

If the plan touches architecture boundaries, explicitly state which decisions
belong to each owner. Do not move routing, delivery, permission, feasibility,
override, retry, persistence, or platform-specific behavior into a semantic
LLM prompt or generic stage unless the user has explicitly approved that
ownership change.

## Agent Autonomy Boundaries

Add a section that constrains parent, production-code subagent, review
subagent, and fallback execution agent judgment:

```md
## Agent Autonomy Boundaries

- The responsible agent may choose local implementation mechanics only when
  they preserve the contracts in this plan.
- The responsible agent must not introduce new architecture, alternate migration
  strategies, compatibility layers, fallback paths, or extra features.
- The responsible agent must treat changes outside the target module as
  high-scrutiny changes. Updating an existing module outside the target module
  or introducing a new code path, prompt, or variable requires strong
  justification in the plan before implementation.
- The responsible agent may remove code from the existing codebase with lighter
  justification when the removal is explicitly in scope and verified by
  references, greps, and tests.
- If the responsible agent is allowed to implement a helper or function, the
  responsible agent must search the codebase first for existing equivalent
  behavior. If equivalent behavior already exists, abstract or move it into an
  appropriate common location instead of duplicating it. For Python code, this
  extraction must follow `py-style` guidance.
- The responsible agent must not perform unrelated cleanup, formatting churn,
  dependency upgrades, prompt rewrites, or broad refactors unless explicitly
  listed in Must Do.
- If the plan and code disagree, the responsible agent must preserve the plan's
  stated intent and report the discrepancy.
- If a required instruction is impossible, the responsible agent must stop and
  report the blocker instead of inventing a substitute.
```

## Context

Describe the current state and why the change exists.

Good context includes:

- the user request or product/problem pressure driving the change
- concrete failure mode, missing capability, maintenance burden, risk, or
  workflow pain that makes the change necessary now
- old architecture and new architecture
- exact state/data shape changes
- production vs test-only status
- known consumers
- external systems affected
- why legacy behavior is being removed or preserved
- relevant previous attempts, completed plans, incidents, or code comments if
  they explain constraints
- adjacent improvement areas discovered during planning that are intentionally
  deferred

When listing adjacent improvement areas, keep them concise and non-authorizing.
They are context for future planning, not permission for the parent or
production-code subagent to expand scope:

```md
## Context

This change is driven by ...

Adjacent improvement areas intentionally left for later plans:

- ...
- ...
```

## Target State

Describe the observable completed behavior, not just files changed. Include the
new ownership boundary, public entrypoint, state shape, call path, prompt
surface, database state, or operational state when those are part of the plan.

## Design Decisions

Use a decision table for meaningful choices:

```md
## Design Decisions

| Topic | Decision | Rationale |
|---|---|---|
| Cache invalidation | Use Cache2 dependency events | Cache1 version counters are removed. |
| State payload | Use hybrid `rag_result` | Structured image data is needed, raw search blobs are too large. |
```

Only include settled decisions. Do not list alternatives unless the alternative
is clearly rejected and the rejection helps prevent future agent drift.

## Contracts And Data Shapes

Add this section for architecture, pipeline, schema, prompt-interface, or new
module changes. Otherwise, fold small contract notes into `Target State` or
`Design Decisions`.

When this section is present, define new contracts explicitly:

- function signatures
- state keys
- payload shape
- input/output cardinality
- ownership boundaries
- refusal or failure conditions
- latency or call-count expectations, if relevant

Prefer precise examples:

```python
{
    "answer": str,
    "user_image": dict,
    "conversation_evidence": list[str],
    "supervisor_trace": {
        "loop_count": int,
        "unknown_slots": list[str],
    },
}
```

Define forbidden compatibility shapes when relevant:

```md
Do not preserve or recreate the legacy `research_facts` / `research_metadata`
payload.
```

For new modules, include a dedicated interface contract. The interface can be
code signatures, protocol messages, schemas, endpoint shapes, CLI commands,
event payloads, or another concrete boundary appropriate to the codebase. The
contract must be specific enough that existing code can integrate with the
module without importing internals.

## LLM Call And Context Budget

If a plan adds, removes, rewires, or changes any prompt, model call, agent
graph, evaluator, RAG/cognition/dialog stage, or background LLM job, include a
before/after LLM budget before finalizing.

Use `50k tokens` as the default context-window cap unless the user explicitly
sets another cap.

For each affected LLM call, state the before/after call count, whether it is
response-path or background, the model/helper used if known, the context inputs
before and after, estimated maximum context use versus the cap, latency impact,
blocking behavior, hard caps, truncation/drop policy, and verification tests.
If exact tokenization is unavailable, use conservative character-based
estimates and state the method. New response-path calls or cap increases
require explicit user approval.

## Change Surface

Separate files into clear groups:

```md
## Change Surface

### Delete
### Modify
### Create
### Keep
```

For each path, explain why it is in that group. Use stable file paths and
symbols. Line numbers may be included as hints, but never rely on line numbers
alone.

When creating a new module, list the module's public entrypoint separately from
its internals. Existing code should depend on the public entrypoint, not on
private storage, prompt, cache, or helper files.

Every plan must name the target module or target ownership boundary. Any
change outside that boundary must include strong justification in
`Change Surface` or `Design Decisions`, especially when it updates an existing
module or introduces a new code path, prompt, or variable. Strong justification
means the plan explains why the target module cannot own the behavior, why the
outside change is necessary for the approved contract, and how tests or greps
will prove the change stayed bounded.

Code removal is less constrained by this rule when the removal is already in
scope. For delete-only work, require enough evidence to show the deleted code
is obsolete or unreferenced, but do not force the same level of justification
required for adding or expanding behavior outside the target module.

## Execution Model

Every final executable plan must state how the work will be executed. Use this
shape unless a user explicitly approves fallback execution:

```md
## Execution Model

- Parent agent owns orchestration, test code, verification, execution evidence,
  review feedback remediation, lifecycle updates, and final sign-off.
- Parent agent establishes the focused test contract first and records the
  expected failure or baseline before production implementation starts.
- Production-code subagent: exactly one native subagent, started after the
  focused test contract is established; owns production code changes only; does
  not edit tests unless the parent explicitly directs it; closes after planned
  production code changes are complete, excluding review fixes.
- Parent agent may continue integration tests, regression tests, static checks,
  and validation work while the production-code subagent edits production code.
- Independent code-review subagent: exactly one native subagent, started after
  planned verification passes; reviews the plan, diff, and evidence; reports
  findings to the parent; does not implement fixes.
- If native subagent capability is unavailable, stop before execution unless
  the user explicitly requests fallback execution.
```

## Independent Plan Review

Use this optional section when the user asks for approval review,
creativity-tightening, stage-boundary review, architecture alignment review, or
handoff review before execution. Place it after `Verification` or before
`Approval`/`Acceptance Criteria`, whichever fits the plan shape.

Use this shape:

```md
## Independent Plan Review

Run this gate before approval, execution, or handoff. Prefer a reviewer that
did not draft the plan. If no separate reviewer is available, the drafting agent
must reread the parent architecture plan, completed prior-stage artifacts, this
plan, and relevant source/test context from a fresh-review posture.

Review scope:

- Previous-stage artifacts are named, completed, and carried forward.
- The proposed scope aligns with the project modular design and the top-level
  architecture plan.
- The stage is unblocked: dependencies, decisions, status, registry rows, and
  required artifacts are present.
- The plan gives full, concrete instructions for execution agents:
  contracts, change surface, exact file paths, verification gates, progress
  checklist, and evidence requirements.
- Agent creativity is tightly bounded: no unresolved choices, broad verbs,
  optional fallbacks, compatibility shims, private helper freedom, or unowned
  side paths remain.
- Boundaries between this stage, previous stages, and next stages are explicit,
  with clean handoff and no overlapping or missing ownership.

Record blockers, non-blocking findings, required edits, open questions, and
approval status. Approve only when blockers are resolved. If blockers remain,
ask specific questions or update the plan before execution.
```

Do not use independent plan review to authorize new scope. If review discovers
new work, add it to the current plan only when it is required for the approved
goal; otherwise create or reference a follow-up plan.

## Independent Code Review

Every final executable plan must include an independent code review section.
Place it after `Verification` and before `Acceptance Criteria`, and include a
matching final progress-checklist checkpoint. The review happens after all
planned implementation verification has passed and before marking the plan
completed, updating lifecycle records to completed, merging, or signing off.
Normal execution uses the independent code-review subagent for this gate.

Use this shape:

```md
## Independent Code Review

Run this gate after all `Verification` commands pass and before final sign-off.
The parent agent must create one independent code-review subagent through the
current harness's native subagent capability. If native subagents are
unavailable, stop unless the user explicitly approves fallback execution.

Review scope:

- Project rules and style compliance for every changed Python, test, prompt,
  documentation, and command artifact.
- Code quality and design weaknesses, including ownership boundaries, hidden
  fallback paths, compatibility shims, prompt/RAG payload leaks, persistence
  risk, brittle fixtures, and avoidable blast radius.
- Alignment with `Must Do`, `Deferred`, `Agent Autonomy Boundaries`, `Change
  Surface`, exact contracts, implementation order, verification gates, and
  acceptance criteria.
- Regression and handoff quality, including prior-stage artifacts, focused and
  regression tests, execution evidence, next-stage handoff notes, and
  path-safe commands for directories containing spaces.

The parent agent fixes concrete findings directly only when the fix is inside
the approved change surface or this review gate explicitly allows review-only
fixture/documentation corrections. If a fix would cross the approved boundary
or alter the contract, stop and update the plan or request approval before
changing code.

Record findings, fixes, commands rerun, residual risks, and approval status in
`Execution Evidence`.
```

If a plan intentionally uses an external reviewer or a named review artifact,
state that exact handoff. Do not leave the reviewer identity, diff baseline,
review scope, or remediation authority open-ended.

## Acceptance Criteria

Acceptance criteria describe the observable completed state.

```md
## Acceptance Criteria

This plan is complete when:

- There is exactly one active RAG path.
- Legacy RAG1 modules and Cache1 modules are deleted.
- No source file imports Cache1 or RAG1.
- New tests for projection and Cache2 invalidation pass.
- Legacy MongoDB collections are absent through the approved migration path.
```

## Risks

Use this section only for `large`, `high_risk_migration`, data, production,
security, prompt, or operational-risk plans. Keep it compact:

```md
## Risks

| Risk | Mitigation | Verification |
|---|---|---|
| Stale cache after conversation save | Emit Cache2 event in `save_conversation` | Cache invalidation test and live smoke |
```
