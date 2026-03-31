# CONTRIBUTING.md

## Guidelines

This repository expects AI code-generation output to include full-file contents when modifying or creating source files. When requesting or providing patches, the assistant MUST output the complete file text so it can be copied-and-pasted directly into the project without manual merging of partial diffs.

### Rule: Full-file output
- For any change to an existing source file (e.g., `.cpp`, `.h`, shaders), the assistant MUST provide the entire file contents.
- The assistant should place the updated full file inside a single code block labeled with the language and target file path, matching project formatting conventions.
- Do not provide only snippets, patches, or line diffs unless explicitly requested by the developer or maintainer.

### Why
- Ensures consistency with the project's toolchain and reduces merge/merge-conflict risk when applying AI-suggested edits.
- Simplifies copy/paste workflow for maintainers using Visual Studio 2022.

## Standards for AI-generated files
- Use the project's .editorconfig rules if present. If .editorconfig is missing, create one consistent with the project's coding style.
- Always ensure generated C++ code compiles with the project's C++ standard (C++14) and includes necessary headers.
- Keep functions and symbols consistent with declarations in headers; avoid introducing linkage mismatches.

## Workflow
1. User asks for a change to a file.
2. Assistant responds with a single code block containing the complete contents of the modified file, using the format:

```<language> <target file path>
<entire file contents>
```

3. The maintainer copies the file content into the project or replaces the existing file.

## Exceptions
- Small clarifying edits to documentation or one-line fixes may be returned as diffs only when expressly requested.
- When generating many files at once, assistant may return a structured list, but each file must still be provided as a complete file block.

## Contact
If this policy needs modification, open an issue or contact the repository maintainers.