# Cline Workflow – Plan & Implement **One** Non-`(Fix)` Task from `DEV_TASKS.md`

## Objective
Scan `DEV_TASKS.md` for tasks that **do not** begin with `(Fix)` and select **exactly one** valid candidate. Produce a concise implementation plan, then implement the plan with minimal, correct code changes. Commit once at the end.

---

## Candidate Detection
- **Eligible task**: any Markdown bullet that starts with `- ` and **does not** begin with `(Fix)` (case-insensitive) immediately after the dash.
- Regex (multiline, anchors at line start):
  ```
  ^(?<indent>\s*-\s*)(?!\((?:[Ff]ix)\))(?<task>.+)$
  ```
- **HTML comment metadata**: if an immediate HTML block follows the bullet (`<!-- ... -->`), it belongs to that task for context (assignee/assigner/status). Preserve it verbatim; do **not** modify metadata.

### Exclusions
- Ignore purely descriptive lines that are not bullets.
- Ignore empty bullets or bullets with only whitespace.
- Ignore tasks that explicitly say “investigate” unless the text names concrete files/paths/functions to ground an implementation.

---

## Selection Policy (pick **one**)
1. Prefer the **first** eligible task in file order that references concrete files or areas of the codebase (e.g., mentions `ENGINE/...`, filenames, panels, or systems).
2. If the first eligible task is ambiguous, skip to the next eligible one.
3. Determinism: always choose the **first** eligible concrete task by the above rule.

Record the selected task’s original bullet line (for the commit message) and any following HTML comment block (do not edit it).

---

## Planning Template (keep concise)
For the selected task, create a short plan with the following headings. Keep each heading to 1–3 bullet lines.

**Plan – _Task Title_**
- **Problem & Expected Behavior:** _1 line; what’s wrong now vs. what it must do._
- **Files & Touch Points:** _Paths + functions/classes/regions if known._
- **Changes (Minimal Scope):** _Ordered list of steps you will modify/add/remove; avoid scope creep._
- **Manual Validation:** _How to sanity check behavior in the app/editor without adding tests._

> Keep the plan adjacent to the code changes in your working notes; do **not** commit plan files to the repo unless the repo already includes a plans/specs directory and the selected task states to add one.

---

## Implementation Steps
1. **Index & Search**
   - Use the **task text** as search seed.
   - Search the repository (prioritize `ENGINE/` and files explicitly named in the task) to locate the current implementation, data structures, and relevant call sites.
   - Open and read code to ground the expected change.

2. **Create a Working Branch**
   - If the repo uses branches, create `feat/task-{slug}`; else proceed in-place (Cline environments vary).

3. **Make Minimal, Focused Code Changes**
   - Modify only the files necessary to satisfy the selected task.
   - Maintain existing conventions, includes, and style.
   - Avoid unrelated refactors or formatting changes.
   - If public APIs change, update all local call sites within the repo (no partial breaks).

4. **Build (or compile-check)**
   - If the repo has a build system, run a local build/compile to catch errors.
   - Resolve any introduced warnings/errors related to the change.

5. **Manual Validation**
   - Follow the plan’s validation steps.
   - If a UI/editor action is involved, describe the quick action sequence (e.g., “open Room Editor → drag light while zooming…”). Do not add automated tests.

6. **Amendments**
   - If behavior is not achieved, apply minimal follow-up changes; revalidate.

7. **Commit**
   - Single commit with message:
     ```
     Implement: <original bullet text>
     
     - Plan: <very short summary, 1–2 lines>
     - Files: <edited paths, comma-separated>
     ```

---

## Guardrails
- **Non-Goals:** Do not rewrite tasks, metadata, or other bullets in `DEV_TASKS.md`. Do not touch the “Dev Tasks” section in `README.md`.
- **Preserve HTML metadata** blocks in `DEV_TASKS.md` (no edits).
- **Scope discipline:** Only implement the **one** selected task. No multi-task batching.
- **No speculative features:** Only what is needed to meet the expected behavior.

---

## Deliverables
- Code changes implementing the selected task.
- A single commit as described above.
- (Optional) If the repo’s conventions include a `CHANGES.md` or similar, append an entry **only if** that practice already exists.

---

## Quick Pseudocode (Task Scanner)
```text
read DEV_TASKS.md
for each line:
  if line matches bullet regex and NOT (Fix):
    capture bullet and potential immediate <!-- ... --> block
    if bullet text references concrete files/paths:
      select this as the candidate and stop
if no candidate found:
  stop with note: “No eligible non-(Fix) task with concrete scope found.”
```

---

## Example (Informative, Do Not Hardcode)
- Selected bullet:
  `- Light blend mode: add blend mode option to ENGINE/utils/light_source.hpp and ENGINE/asset/asset_info.hpp; implement norm blend rendering in scene renderer; allow switching between mod blend and norm blend for light objects.`
- Plan (abbreviated):
  - Problem/Expected: Cannot toggle blend; add selectable mode.
  - Files: `ENGINE/utils/light_source.hpp`, `ENGINE/asset/asset_info.hpp`, renderer where light is drawn.
  - Changes: enum BlendMode { Mod, Norm }; plumb through asset info; switch in renderer; default preserved.
  - Validation: Toggle mode in UI → observe mask vs normal blend difference on light.
- Commit message:
  ```
  Implement: Light blend mode: add blend mode option to ENGINE/utils/light_source.hpp and ENGINE/asset/asset_info.hpp; implement norm blend rendering in scene renderer; allow switching between mod blend and norm blend for light objects.

  - Plan: add enum + plumb + renderer switch; default remains Mod
  - Files: ENGINE/utils/light_source.hpp, ENGINE/asset/asset_info.hpp, ENGINE/render/...
  ```
