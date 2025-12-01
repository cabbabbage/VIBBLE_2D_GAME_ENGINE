# task

Cline workflow: generate a clear, expanded task description from additional context in chat.

This workflow is not for directly editing code. Its job is to read the user’s additional context (given as a `<Task>` block in the chat) **or** infer the task automatically if none is provided.  
It then outputs a clean, detailed task description that any model or human can understand and act on.

The expanded task description must be printed in the **Cline response window**.

---

## Purpose

Turn a raw `<Task>` block from the chat, or the current `log.txt` contents, into a precise, structured task description that:

- Summarizes what is broken or needs to change.
- Lists the relevant files, systems, and constraints.
- Explains the expected kind of fix or modification.
- Provides enough clarity that:
  - A human developer, or  
  - Another model (given the `context/` folder)  
  can understand the problem and how to approach fixing it.

This workflow does not perform code changes. It only produces a refined description of the task.

---

## Inputs

- Chat history that includes an additional context block of the form:

  ```text
  <Task>
  ...user’s raw task description, notes, errors, thoughts...
  </Task>
