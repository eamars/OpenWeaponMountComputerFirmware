# Execution Gates Reference

Read this reference for every final executable plan. It owns granular execution
steps, implementation order, progress checklist, verification, execution
evidence, handoff, and review-derived accuracy gates.

## Table Of Contents

- [Parent/Subagent Execution Model](#parentsubagent-execution-model)
- [Implementation Order](#implementation-order)
- [Granular Execution Steps](#granular-execution-steps)
- [Progress Checklist](#progress-checklist)
- [Independent Plan Review Gate](#independent-plan-review-gate)
- [Plan Self-Review](#plan-self-review)
- [Verification](#verification)
- [Independent Code Review Gate](#independent-code-review-gate)
- [Review-Derived Accuracy Gates](#review-derived-accuracy-gates)
- [Execution Evidence](#execution-evidence)
- [Execution Handoff](#execution-handoff)

## Parent/Subagent Execution Model

Development-plan execution is parent-led and requires native subagent
capability unless the user explicitly requests fallback execution. Use the
current harness's native delegation feature rather than depending on a specific
framework or tool name.

Default ownership:

- The parent agent owns orchestration, test code, test validation, static
  checks, execution evidence, review feedback remediation, lifecycle updates,
  and final sign-off.
- The production-code subagent owns planned production code changes only. It
  receives the approved plan, mandatory skills, change surface, focused test
  contract, and production ownership boundary. It must close after planned
  production code changes are complete, excluding review fixes.
- The independent code-review subagent owns review only. It receives the
  approved plan, full diff, verification evidence, and review scope. It reports
  findings to the parent and does not implement fixes.

Normal execution uses exactly two subagents in sequence:

1. One production-code subagent after the parent establishes the focused test
   contract.
2. One independent code-review subagent after planned implementation
   verification passes.

The parent must establish the focused test contract before production
implementation starts. After that gate, production code and broader
test/verification work may proceed in parallel: the production subagent edits
production code while the parent continues integration tests, regression tests,
validation scripts, static checks, evidence, and plan-progress updates.

If native subagent capability is unavailable, stop before execution and report
the blocker. Do not silently switch to single-agent execution. A fallback path
is valid only when the user explicitly asks for it.

## Implementation Order

Implementation order must prevent avoidable breakage and agent improvisation.

Default to a parent-owned test-contract-first order. The plan should prove the
module contract before production implementation, then prove integration after
the module is stable. For LLM, database, migration, or production-path changes,
include the focused module test and the relevant integration, real LLM, real
database, migration dry-run, or smoke test needed to cover the actual risk.

Every final plan must include this general sequence unless a step is truly
inapplicable:

1. Parent adds or updates module tests.
   - Name the exact test file, test function, fixture, or diagnostic that
     proves the target module contract.
   - Run the module test before implementation when applicable and record the
     expected failure, missing symbol, missing entrypoint, or baseline result.
2. Parent starts the production-code subagent.
   - Provide the approved plan, mandatory skills, change surface, focused test
     contract, and exact production-code ownership boundary.
   - The subagent edits production code only and reports changed files,
     commands run, blockers, and residual risks before closing.
3. Parent adds or updates integration tests.
   - Name the exact integration, real LLM, real database, migration, smoke, or
     cross-module test that proves callers can use the module correctly.
   - Run the integration test before implementation when practical and record
     the failure mode or current behavior.
   - This work may happen while the production-code subagent implements
     production code.
4. Production-code subagent implements the module.
   - Keep the behavior inside the target module and approved public interface
     whenever possible.
   - Do not update existing outside modules or introduce new code paths,
     prompts, or variables without the strong justification required in
     `Change Surface`.
5. Parent runs module tests.
   - Re-run the same module test from step 1.
   - Record the result in `Execution Evidence`.
6. Loop back to step 1 if needed.
   - If the module test fails or reveals an incomplete contract, update the
     module test or module implementation only to match the approved plan, then
     repeat the focused test, production implementation, and module-test gates
     before touching integration.
7. Parent implements integration or directs production-code ownership changes.
   - Wire existing callers, adapters, prompts, database paths, or service
     entrypoints to the module only after module tests pass.
   - Keep integration edits inside the approved change surface.
8. Parent runs integration tests.
   - Re-run the same integration test from step 3.
   - Then run any broader verification gates listed in `Verification`.
   - Record the result in `Execution Evidence`.
9. Loop back to step 1 if needed.
   - If integration exposes a module contract problem, return to module tests
     before changing integration again.
   - If integration exposes only wiring behavior, update integration and re-run
     the integration test.
10. Parent starts the independent code-review subagent.
   - Review the full implementation diff against project style, code quality,
     plan alignment, design risk, regression coverage, handoff artifacts, and
     verification accuracy.
   - The review subagent reports findings and approval status; it does not own
     implementation fixes.
   - Record findings, fixes needed, commands rerun, residual risks, and review
     approval status in `Execution Evidence`.
11. Parent remediates review findings and repeats required verification.
    - Fix findings directly only when the fix is inside the approved change
      surface or the plan explicitly allows review-only corrections.
    - If a finding requires a contract, boundary, or change-surface change,
      update the plan or request approval before changing code.
    - Rerun affected focused tests, static checks, and regression gates before
      final sign-off.

For low-risk documentation-only plans, explicitly state that the
module/integration test-first sequence is not applicable and replace it with a
before/after review gate. The native subagent requirement still applies unless
the user explicitly approves fallback execution.

Additional ordering guidance:

- create the new contract first
- rewrite consumers next
- wire the new entrypoint
- delete legacy modules after references are gone
- run greps and smoke tests last

Include a short rationale when order matters:

```md
Build the projection module first because it becomes the contract used by
cognition, consolidation, and tests.
```

## Granular Execution Steps

Execution stages must decompose into small, verifiable steps. A step is too
broad if a parent or execution subagent could complete it in multiple
incompatible ways without violating the wording.

Default step shape:

- one action with one clear target
- exact file path, symbol, test, command, or artifact
- expected result or evidence before moving on
- no hidden design choice

For low-risk implementation work, aim for steps that take about 2-5 minutes:

```md
- [ ] Step 1 - add the failing serializer test
  - File: `tests/test_serializer.py`
  - Action: add `test_serializer_rejects_unknown_kind`.
  - Verify: `pytest tests/test_serializer.py::test_serializer_rejects_unknown_kind -q`
  - Expected before implementation: fails because `UnknownKindError` is not raised.
```

For high-risk architecture, prompt, migration, database, or production-path
work, do not force artificial 2-minute fragments. Use the smallest step that
preserves reviewability, rollback clarity, and a single decision boundary.

Use TDD triplets whenever code behavior changes:

1. write or update the focused failing test
2. run it and record the expected failure
3. implement the minimal change
4. rerun the same test and record the pass

Do not write broad steps such as:

- "implement the module"
- "add tests"
- "update callers"
- "handle errors"
- "wire everything together"

Replace them with named files, symbols, tests, commands, expected failures, and
expected passes.

Code snippets are optional. Include complete snippets only when the code is
stable, narrow, and unlikely to become stale before execution. For high-risk or
fast-moving code, prefer exact contracts, signatures, state shapes, invariants,
and verification commands over copy-paste implementation bodies.

## Progress Checklist

Every final plan must include a tickable progress checklist using Markdown
checkbox syntax (`- [ ]` or `1. [ ]`). These are the progress boxes the parent
or active execution agent updates as each function, module, integration step,
or sign-off gate is completed. Small plans may have a short checklist, but
they still need one so progress and handoff state are visible.

Checkpoints exist so multiple agents can resume the work without rediscovering
state or guessing which partial changes are complete. They should be granular
enough that an agent can finish one checkpoint, verify it, mark it complete,
and hand off cleanly.

Each tickable checkpoint must describe:

- the stage, function, module, interface, integration, or sign-off gate being
  completed
- the files or modules expected to be touched
- the verification commands or static checks to run before ticking the box
- the evidence that must be recorded before ticking the box
- the next checkpoint or next implementation step
- the sign-off line the agent must complete after that checkpoint is done
- the granular execution steps inside the checkpoint, or a pointer to the
  numbered implementation steps it covers
- the final independent code review checkpoint, including review scope,
  remediation authority, commands to rerun after fixes, and sign-off evidence

```md
## Progress Checklist

- [ ] Stage 1 - module contract established
  - Covers: steps 1-3.
  - Verify: `python -m py_compile ...`; focused module tests pass.
  - Evidence: record changed files and test output in `Execution Evidence`.
  - Handoff: next agent starts at Stage 2.
  - Sign-off: `<agent/date>` after verification and evidence are recorded.
- [ ] Stage 2 - service integration complete
  - Covers: step 4.
  - Verify: service integration tests pass.
  - Evidence: record test output before moving on.
  - Handoff: next agent starts at Stage 3.
  - Sign-off: `<agent/date>` after verification and evidence are recorded.
```

Do not treat checked boxes as proof by themselves. An agent may tick a
checkpoint only after running its verification and recording the result in
`Execution Evidence` or a linked execution record. If verification is skipped
or blocked, leave the box unchecked and record why.

The parent or active execution agent must sign off stages one at a time,
immediately after each stage is genuinely complete. Do not pre-fill sign-offs,
do not sign off future stages, and do not batch multiple stage sign-offs at the
end of a session. If handing off mid-plan, leave all unfinished stages
unchecked and unsigned, and add a brief handoff note that points to the next
unchecked stage.

After signing off any major checklist stage, the parent or active execution
agent must reread the entire plan before starting the next stage. This reread
requirement must be written into the plan's `Mandatory Rules` and treated as
part of each major stage sign-off gate.

Every executable plan must include a final progress-checklist checkpoint for
independent code review by the review subagent. The plan cannot be marked
completed, merged, or signed off until this checkpoint is verified and evidence
is recorded. The checkpoint must remain unchecked if the review is skipped,
blocked, or finds unresolved issues.

## Independent Plan Review Gate

Use this optional pre-approval gate when the user asks for plan approval review,
creativity-tightening, stage-boundary review, architecture alignment review, or
handoff review. It runs before plan approval, execution, or handoff, not after
implementation.

The gate must inspect these inputs:

- the current plan draft
- the parent or high-level architecture plan
- completed previous-stage plans, evidence, registry rows, and artifacts
- planned next-stage handoff requirements when the current stage feeds later
  work
- relevant source and tests when plan readiness depends on existing contracts

The review checklist must cover:

- **Architecture alignment:** project modular design, ownership boundaries,
  adapter/brain/RAG/cognition/dialog/persistence responsibilities, LLM-owned
  semantic judgment, and deterministic-code responsibilities.
- **Stage readiness:** status, blockers, previous artifacts, dependencies,
  user decisions, and registry or ledger handoff are present and current.
- **Instruction completeness:** exact contracts, file paths, change surface,
  implementation order, verification commands, progress checklist, evidence,
  and acceptance criteria are specific enough for parent and execution agents.
- **Creativity suppression:** no unresolved choices, broad verbs, helper
  freedom, alternate call paths, fallbacks, compatibility shims, or unbounded
  "similar to" wording remains.
- **Stage boundaries:** previous-stage carryover, current-stage ownership,
  deferred work, next-stage handoff, and non-overlap between plans are explicit.

Findings should be classified as blockers, non-blocking findings, or questions.
Blockers must be resolved before approval or execution. Non-blocking findings
may be recorded with rationale. If the review raises a question that changes
scope, ask the user before approving the plan.

## Plan Self-Review

Before marking a plan approved or ready for implementation, perform and record a
brief self-review:

- **Coverage:** every `Must Do`, accepted design decision, and acceptance
  criterion maps to at least one implementation step or verification gate.
- **Minimality:** the `Overdesign Guardrail` names the actual problem,
  minimal change, ownership boundaries, rejected complexity, and evidence
  threshold; no new field, layer, flag, helper, fallback, mode, or abstraction
  lacks current-requirement justification.
- **Placeholder scan:** no `TBD`, `TODO`, "similar to", "handle edge cases",
  "add tests", or open-ended implementation wording remains.
- **Contract consistency:** filenames, function names, state keys, schemas,
  prompt variables, test names, and command paths match across sections.
- **Granularity:** no checkpoint hides multiple unrelated edits, and every step
  has a target plus expected evidence.
- **Verification:** each important behavior change has a focused check and an
  expected result.

Fix issues inline before approval. If the self-review finds a missing decision,
return to discovery instead of guessing.

## Verification

Verification must be written as gates, not vague reminders. Include exact
commands or checks:

```md
## Verification

### Static Greps

- `rg "research_facts|research_metadata" src` returns no matches.

### Tests

- `pytest tests/test_rag_projection.py`
- `pytest tests/test_save_conversation_invalidation.py`

### Smoke

- Service boots without missing import errors.
- One chat request returns a non-empty response.

### Database

- `rag_cache_index` and `rag_metadata_index` are absent after migration.
```

State allowed exceptions directly beside each check.

For every static check, state the exact expected result:

- whether zero matches are expected and which nonzero exit code is acceptable
  for tools such as `rg`
- which matches are allowed, if any, and the file or literal context that makes
  them acceptable
- which matches are forbidden and what the agent must do if they appear

## Independent Code Review Gate

The independent code review gate is mandatory for executable plans and runs
after verification, before completion or merge. Normal execution requires an
independent code-review subagent. It exists to catch the problems that normal
implementation verification often misses: style drift, weak design choices,
plan deviations, stale static-grep expectations, missing handoff artifacts, and
unplanned file changes.

The gate must inspect these inputs:

- the approved plan and any completed parent or prior-stage artifacts
- `git diff` or equivalent changed-file output from the implementation branch
- focused tests, regression tests, static checks, and execution evidence
- lifecycle records, registry rows, and handoff notes when the plan updates
  them

The review checklist must cover:

- **Project rules and style:** mandatory skill compliance, docstrings,
  imports, defaults, exception handling, helper shape, CJK safety, test style,
  and command path safety.
- **Plan alignment:** `Must Do`, `Deferred`, autonomy boundaries, change
  surface, exact contracts, implementation order, verification gates,
  acceptance criteria, and lifecycle evidence.
- **Code quality and architecture:** module ownership, call paths, fallback or
  compatibility behavior, prompt/RAG/context leaks, persistence and scheduler
  boundaries, fixture brittleness, and avoidable blast radius.
- **Regression and handoff:** prior-stage artifacts carried forward, baseline
  tests preserved, new tests mapped to risks, stale static gates corrected, and
  next-stage handoff left clean.

Concrete findings should be fixed by the parent before sign-off when they are
inside the approved change surface or inside an explicit review-fix allowance.
If a finding requires new scope, a different public contract, or edits outside
the approved boundary, the parent must stop and update the active plan or
request approval before implementing the fix.

The review outcome must be recorded as evidence. A useful evidence line states
the review subagent identity or harness role, files reviewed, findings, fixes
made, verification rerun, residual risks, and whether the plan is approved for
completion.

## Review-Derived Accuracy Gates

When internal or external review finds stale wording, expectation drift,
missing handoff artifacts, or avoidable implementation freedom, update the
active plan before approving or continuing execution. Completed plans may
receive only factual evidence corrections or links to a new follow-up plan.

Do not leave placeholder verification text such as "copy from the previous
stage later" once the previous stage has completed. Carry forward the actual
completed artifact names, commands, and evidence needed by the next agent.

Do not authorize private helpers, wrappers, aliases, or abstraction layers just
because they are convenient. A plan may permit a helper only when it removes
nontrivial repeated structural validation, performs local table lookup, or
matches an established project pattern. Raising-only helpers, pass-through
wrappers, feature flags, fallback paths, and alternate call sites must be
forbidden unless the plan explicitly justifies why they are part of the
contract.

## Execution Evidence

Do not treat checked boxes as proof. If the plan also records completion, add a
separate evidence section:

```md
## Execution Evidence

- Static grep results:
- Test results:
- Service boot:
- DB verification:
- Manual smoke:
```

Pre-execution plans should use unchecked checklist items. If a plan is
completed, either move checked items into an execution record or attach evidence
for the checks.

Never document scope creep in a completed plan. Once complete, the plan may
receive only factual corrections to execution evidence or links to a
new/superseding plan. If new requirements appear after completion, keep the
completed plan unchanged and create a separate follow-up plan.

## Execution Handoff

Use this section only for multi-session handoff, interrupted execution, or
transfer to another parent agent. Do not add it to every plan by default.

When needed, state the intended execution mode:

- parent-led native subagent execution
- explicit user-approved fallback execution, only when requested
- sequential handoff to another parent agent, with the next unchecked stage and
  subagent state clearly named

For handoff, name the next unchecked stage, required skills, files expected to
change next, and verification commands to run before sign-off.
