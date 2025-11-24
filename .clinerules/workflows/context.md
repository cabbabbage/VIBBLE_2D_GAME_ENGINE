# Task: Build `context` bundle for focused GPT editing

IMPORTANT: This task must not modify, delete, or rename anything inside the original `ENGINE/` folder.  
You only read and search files under `ENGINE/`. The only files and folders you are allowed to create or modify are:

- `context/` and its contents
- The copied `log.txt` inside `context/`
- The `context_summary.txt` (or similar) inside `context/`
- The generated `engine_struct.txt` inside `context/` (produced by `context.bat`)
- The temporary `context_list.txt` in the repo root

All work must be done using copies. The source files in `ENGINE/` are read only.

HARD LIMIT: When you are done, the `context/` folder must contain no more than 25 `.txt`, `.cpp`, and `.hpp` files total.  
This count includes:

- `log.txt`
- `context_summary.txt`
- `engine_struct.txt`
- Every copied `.cpp` and `.hpp` file

TARGET RANGE: Aim for **between 20 and 25** files total in `context/`, with a strong preference for getting as close to **25** as possible while still keeping only relevant files. Avoid going below 20 if you can reasonably find enough related files.

FLAT LAYOUT RULE: All files in `context/` must be in a flat structure.

- No subfolders under `context/`
- Every copied file lives directly in `context/` (for example: `context\camera.cpp`, `context\grid.hpp`)

Repo root layout assumptions:

- `ENGINE\` is in the repo root
- `context\` is in the repo root
- `context.bat` is in the repo root
- `context_list.txt` (temporary) is in the repo root
- `log.txt` is in the repo root

`context.bat` is responsible for:

- Recreating the `context\` folder
- Copying all files listed in `context_list.txt` into `context\`
- Copying `log.txt` into `context\`
- Generating `context\engine_struct.txt` using `tree ENGINE /F`

---

## Goal

Create a clean `context/` folder that contains:

- Only the C++ source and header files needed to work on the user's current task, plus their most important dependencies and dependants
- The current `log.txt`
- A short text file that explains the task, the relevant parts of the repo, and why each file was included
- A recursive file structure listing of the `ENGINE/` directory created by `context.bat` as `context\engine_struct.txt`

This folder will be zipped and uploaded to another GPT so it has an up to date, focused snapshot of just the files it needs to edit.

---

## Instructions for Cline

Always run these steps from the repo root that contains `ENGINE\`, `log.txt`, and `context.bat`.

---

### 1. Read the user’s task description

1. In the chat, look for a task block of the form:

   <task>  
   ...task would be here...  
   </task>

2. If such a `<task>...</task>` block exists:

   - Treat the contents as the canonical task description
   - Use it as your main source of truth

3. If the user does not provide a `<task>...</task>` block:

   - Assume the task is:
     - Read `log.txt`
     - Identify compile/runtime errors
     - Prepare `context/` so another GPT can fix them

4. Extract the important keywords, classes, systems, file hints.

---

### 2. Discover relevant files under `./ENGINE`

Scan and search through `ENGINE/` using class names, function names, keywords from the task, or symbols from `log.txt`.

Include files only if:

- They directly relate to the task
- They are direct dependencies or dependants
- They are clearly required to understand or modify the feature

Try to walk up/down the dependency tree until the final context size is heading toward 20–25 files.

Avoid collisions in basenames. If unavoidable, plan a manual rename later.

---

### 3. Create and populate `context_list.txt` in the repo root

1. Delete any old `context_list.txt`
2. Create a new one
3. Append each selected file path, relative to repo root, one per line  
   Example:  
   ENGINE\render\camera.cpp  
   ENGINE\render\camera.hpp  
   ENGINE\world\grid.cpp  
   ENGINE\utils\area.hpp  

Estimate the final count:

number_of_cpp_hpp_in_list + log.txt + context_summary.txt + engine_struct.txt  

Adjust list to land between **20 and 25**.

`context_list.txt` must be in the repo root next to `context.bat`.

---

### 4. Run `context.bat` to build `context/`

From repo root:

Run:

context.bat

It will:
- Recreate `context/`
- Copy all files from `context_list.txt`
- Copy `log.txt`
- Generate `context\engine_struct.txt` via:  
  tree "ENGINE" /F > "context\engine_struct.txt"

Then verify:

- `context/` exists
- `context/log.txt` exists (if root log.txt does)
- `context/engine_struct.txt` exists

If `context.bat` fails, note that later in `context_summary.txt`.

---

### 5. Verify copied files and clean up `context_list.txt`

After `context.bat` runs:

1. For each path in `context_list.txt`:

   - Ensure its basename exists in `context/`

2. Fix any missing files, correct paths, and rerun `context.bat` as needed

3. When all files are verified:

   - Delete `context_list.txt` from the repo root

4. Count final files in `context/`:

   Include:
   - copied `.cpp` / `.hpp`
   - `log.txt`
   - `engine_struct.txt`
   - upcoming `context_summary.txt`

If count exceeds 25, prune least important files and later document them.

Keep the total between **20 and 25**.

---

### 6. Create `context_summary.txt` (last step)

Inside `context/`, create `context_summary.txt` containing:

1. Task summary (in your own words)
2. A short repo layout overview showing original paths of files
3. A bullet list explaining why each file was included
4. Notes about filename collisions or dropped files
5. How to use this bundle (zip and upload to GPT)

Plain text only.

This file counts toward the 25 limit.

---

## Deliverables

At the end:

`context/` contains:

- selected `.cpp` and `.hpp` files
- `log.txt`
- `context_summary.txt`
- `engine_struct.txt`

All flat, no subfolders.

`context_list.txt` has been deleted.

No files in `ENGINE/` were modified.

Total files in `context/` = between **20 and 25**, ideally near 25.

The folder is ready to be zipped and uploaded as a focused GPT editing bundle.
