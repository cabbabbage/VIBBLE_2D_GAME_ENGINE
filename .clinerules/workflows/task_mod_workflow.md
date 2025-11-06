# Cline Spec – Rewrite `(Fix)` Tasks in `DEV_TASKS.md` and Sync **Dev Tasks** Section to `README.md`

## Goal
Rewrite every bullet in `DEV_TASKS.md` that starts with `(Fix)` into a single, specific, concise task line (no `(Fix)`), **preserving the HTML comment metadata block that follows each bullet**. After rewriting all `(Fix)` bullets, **synchronize the “Dev Tasks” section in `README.md`** so its contents exactly match the updated tasks from `DEV_TASKS.md` (replace the full section body or append a new section if none exists).

---

## Detection Rules (what to rewrite)
- A “matched `(Fix)` task” is **any Markdown bullet line** that begins with `- (Fix)` or `- (fix)` (allow leading indentation).
- Regex (multiline, ^ anchors at line starts):

  ```
  ^(?<indent>\s*-\s*)\((?:[Ff]ix)\)\s*(?<task>.+)$
  ```

- **Capture groups:**
  - `indent` = original leading spaces + `- ` (preserve exactly).
  - `task` = the original task string text **after** `(Fix)` (used as the seed prompt).
- **Metadata attachment rule:** If the very next non-empty line starts with `<!--`, then **capture the entire HTML comment block** from that `<!--` line through the first matching `-->` line (inclusive). This block **belongs to the matched bullet** and must be preserved verbatim and remain immediately after the rewritten bullet.

---

## Rewrite Strategy (per matched `(Fix)` task)
1. **Seed prompt:** Use the captured `task` text (original text after `(Fix)`) as the search prompt.
2. **Codebase search:**
   - Search the repository for referenced files/symbols/terms from the seed (e.g., filenames like `room_editor.cpp`, classes, functions, “parallax”, “drag”, etc.).
   - Prefer paths under `ENGINE/` and any paths explicitly mentioned.
3. **Synthesize a single-line task** that:
   - Names the **exact files** (and known functions/regions) to touch.
   - States the **current behavior/problem** and the **expected behavior**.
   - Lists **brief implementation scope/steps** (comma-separated, keep tight).
   - Mentions **manual validation** steps (as text in the same line; no tests).
   - **Do not include** the `(Fix)` marker anymore.
4. **Formatting:**
   - Replace only the original bullet line with:  
     `{{indent}}{{rewritten task line without (Fix)}}`
   - Keep it **one bullet line** (wrap naturally by Markdown if needed).
   - Leave the captured HTML comment block **unchanged** and directly following the bullet.

**Example**
- Before:  
  `- (Fix) on asset drag and drop functions, fix screen paralax issues. lock mouse to asset anchoring points for drag operations. @room_editor.cpp`
- After:  
  `- Asset drag/drop parallax: update ENGINE/dev_mode/room_editor.cpp (hit testing + drag loop) to lock cursor to selected asset anchor; unify world↔screen conversions via active camera to remove drift; validate by dragging while panning/zooming across room boundaries.`

---

## Replacement Rules
- **Only** replace the matched `(Fix)` **bullet line**; keep its `indent` exactly.
- **Remove** the `(Fix)` marker from the rewritten line.
- **Preserve verbatim** the immediate HTML comment block and keep it **immediately** after the rewritten bullet.
- **Do not** alter non-`(Fix)` bullets, any other text, or any HTML comment content.
- **Do not** re-order bullets or reformat spacing beyond the bullet line text itself.

---

## Validation (after rewriting all `(Fix)` bullets)
- `DEV_TASKS.md` remains valid Markdown.
- Diff shows **only** changes to the text of matched `(Fix)` bullet lines (metadata blocks untouched).
- Lines without `(Fix)` are untouched.
- Every matched bullet still has its original comment block immediately after it.

---

## README Sync (after tasks are updated)
**Objective:** Ensure `README.md` has a “Dev Tasks” section whose content **exactly mirrors** the tasks list from `DEV_TASKS.md`.

**Source to mirror:** The entire **tasks block** from `DEV_TASKS.md` under the first heading that matches `/^#{1,3}\s*Dev\s+Tasks\b/i`.  
- **Extract** from that heading line **through** (but not including) the next heading of the same or higher level, or end-of-file if none.
- Preserve the tasks’ bullets and their HTML comment blocks verbatim.

**Destination rules (`README.md`):**
1. **If a “Dev Tasks” section exists** in `README.md` (heading matching the same regex, same or any level ≤3):
   - **Replace the full body** of that section with the extracted tasks block from `DEV_TASKS.md` (do not nest headers; keep only the list + its comment blocks).
2. **If no “Dev Tasks” section exists**:
   - **Append** at the end of `README.md`:

     ```
     ## Dev Tasks

     {{pasted tasks block from DEV_TASKS.md}}
     ```

3. Do **not** modify any other sections of `README.md`.

**README Sync Validation:**
- `README.md` contains exactly one “Dev Tasks” section.
- Its content equals the extracted tasks block from `DEV_TASKS.md` (including all bullets and HTML comment blocks, order, and spacing).

---

## Implementation Steps (Cline)
1. **Read** `DEV_TASKS.md`.
2. **Find** all `(Fix)` bullets using the Detection Rules regex; for each:
   - Capture `indent`, `task`, and the optional immediate HTML comment block.
3. **For each matched task**:
   - **Search** the repo using the `task` text; open likely files under `ENGINE/` and any referenced paths/symbols for grounding.
   - **Generate** a single-line, specific, directive rewrite per “Rewrite Strategy”.
   - **Replace only** the matched bullet line (preserving `indent`), **strip `(Fix)`**, and **re-attach** the captured HTML comment block immediately after.
4. **Write** the updated `DEV_TASKS.md`.
5. **Validate** all “Validation” checks for `DEV_TASKS.md`.
6. **README Sync:**
   - **Extract** the “Dev Tasks” section body from the updated `DEV_TASKS.md` as defined above.
   - **Read** `README.md`:
     - If a “Dev Tasks” section exists, **replace its body** with the extracted block.
     - Else, **append** a new `## Dev Tasks` section with the extracted block at the file end.
   - **Write** the updated `README.md`.
7. **Validate** “README Sync Validation”.
8. **Final pass:** Ensure only the intended changes are present in `DEV_TASKS.md` and `README.md`.

---

## Commit Message
```
Rewrite (Fix) tasks in DEV_TASKS.md; preserve metadata blocks; sync README.md “Dev Tasks” section to match updated tasks.

- Rewrite all `(Fix)` bullets in DEV_TASKS.md into single-line, specific tasks (no `(Fix)`), preserving immediate HTML comment blocks verbatim and order.
- Update/append README.md “Dev Tasks” section to exactly mirror the tasks block from DEV_TASKS.md.
```

---

## Non-Goals / Safety
- **Only modify**: `DEV_TASKS.md` (bullet text of matched `(Fix)` items) and `README.md` (the “Dev Tasks” section body or appended section).
- Do **not** change assignee/assigner/status HTML comment blocks.
- Do **not** add/remove tasks or re-order bullets.
- Do **not** introduce or edit tests or code.


---

## Repository State Alignment (README only, non-tasks)

After syncing the **Dev Tasks** section, perform a **minimal, accurate refresh of `README.md`** so it reflects the **current repository state**. This step must **not** modify the Dev Tasks section you just replaced.

### Scope
- **Allowed data sources:** `README.md` and the local directory tree under `./ENGINE/`.
- **Out of scope:** Any other files or directories outside `./ENGINE/`, network calls, and speculative edits.

### What to update (examples, do only if verifiably stale):
- File/path references (e.g., modules or panels) that no longer exist under `./ENGINE/` → update or remove.
- New top-level `./ENGINE/` modules that are clearly intended to be listed in README feature/module summaries → add.
- Build/run snippets or commands that reference moved/renamed files **visible under `./ENGINE/`** → correct.
- Headings or short descriptions that explicitly claim a file lives in a path that differs from what exists in `./ENGINE/` → align wording to match.
- Image or code-block captions mentioning `./ENGINE/…` paths → correct paths only.

### Explicit **do-nots**
- **Do not** alter the **Dev Tasks** section content you just replaced.
- **Do not** infer or invent new features; only reflect what is confirmed by `./ENGINE/` or already stated in `README.md`.
- **Do not** change tone, structure, or long-form prose beyond the minimal edits needed for accuracy.
- **Do not** reorder major sections of the README.

### Implementation Steps
1. Read `README.md` and **snapshot** the boundaries of the **Dev Tasks** section (from its header to just before the next header of the same or higher level). Mark this region **read-only** for this step.
2. Traverse `./ENGINE/` (one level deep is sufficient) to collect current module/folder names and key files commonly referenced by README (e.g., `dev_mode`, `render`, `assets`, etc.).
3. Scan `README.md` **outside the protected Dev Tasks region** for:
   - Lists of modules, paths, or file references under `./ENGINE/`.
   - Build/run snippets that include explicit `./ENGINE/` paths.
   - Headings/paragraphs that state a specific path under `./ENGINE/`.
4. For each mismatched reference, apply the **smallest possible edit** to correct the path/name. Prefer path-only edits over sentence rewrites.
5. Write back `README.md`, preserving the Dev Tasks section verbatim.

### Validation
- The **Dev Tasks** section is byte-for-byte identical to the one produced in the prior step.
- All applied edits are limited to `README.md` **outside** the Dev Tasks section.
- Each path change is justified by an actual file/folder under `./ENGINE/`.
- No speculative or stylistic changes were made.

