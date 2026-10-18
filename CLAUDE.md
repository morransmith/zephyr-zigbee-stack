# CLAUDE.md

## General

- All repository content is in English.
- Public symbols use the `zzb_` prefix, public headers go in `include/zzb/`, Kconfig symbols are `CONFIG_ZZB_*`.
- Spec references in code use the form `R22 4.3.1.1` (Zigbee PRO R22, 05-3474-22), `BDB 10.2.5` (BDB 3.0.1), `ZCL 2.5.7` (ZCL rev 8). Add one only where the code does not show which clause it implements.

## Third-party code

- Code under Apache-2.0, MIT, BSD or 0BSD may be adapted. Keep the original copyright notice in the file and add the source to `NOTICE`.
- Code under GPL or LGPL may be read to understand behavior. Never copy or transcribe it.
- Vendor Zigbee SDKs (Silicon Labs, TI, NXP, Microchip, Nordic ZBOSS) must not be used.

## Build and test

```
west build -b native_sim/native/64 tests/unit/<layer>
west twister -T tests -p native_sim/native/64
west build -b nrf52840dk/nrf52840 samples/<name>
```

## Where these rules override Claude Code defaults

- Commit messages are one ASCII line with a conventional prefix (feat:, fix:, chore:, ! for breaking). No body except a BREAKING CHANGE: paragraph, and no Co-Authored-By or any other trailer.
- Work happens in a feature/<name> branch off develop. Never merge into develop or master, and never delete a branch.
- Never create a pull request. Never git push unless asked; one request covers one push.

## Writing style

Applies to every document here, and to answers given in conversation.

Say it literally. Do not substitute metaphor or flourish for direct statement. Write "a parameter worth varying", not "a dial worth turning"; "this still matters", not "this earns its keep". Mannered phrasing displays the writer instead of conveying the idea, makes the reader work harder, and is imprecise, because metaphors carry connotations that were not chosen and cannot be controlled. When a literal phrase is available, use it. Established technical terms (cache-warm, hot path, backpressure, single-flight) are not flourish and stay.

Do not define things by antithesis. State what is true and stop. Write "the log records every request", not "the log records every request, not an afterthought"; "the config file is the only source of settings", not "the config file is not a hint for the user - it is the only source of settings". The negated half usually carries no information and exists for rhythm. Two exceptions: prohibitions ("must not", "never") are specifications and stay; a rejected alternative may be named where the reason is given with it.

No sycophancy. Do not open with praise or agreement - no "good question", "you're right", "great idea" - and do not praise the design in the documents. Assessment appears only where it carries information the reader does not already have, with the reason attached. Calling something "elegant" or "non-obvious" is a claim about the writer, not about the subject.

Say each thing once. Do not write claim -> explanation -> restatement -> summary -> closing flourish. A paragraph that adds no fact is deleted, including the last one. Length must come from new facts and necessary distinctions, never from explaining the same point again in different words.

## Code

Tests first. Write the test, watch it fail for the right reason, then write the code that makes it pass. Scope is limited by the test rules below.

Code first, comments second. Write the code, then add a comment where the code cannot carry the meaning alone. Comments written before the code state what the code will state, so they end up redundant.

Comments are allowed only where they explain something non-obvious. Keep them to a line or two; no multi-paragraph prose.

Early exit. Return, raise or continue as soon as a case is decided. Rewrite nested conditionals as guard clauses and keep the main path at one indent level.

Short and plain. Prefer the version a human reads faster: fewer branches, fewer locals, direct names. Replace code that needs a comment to be understood with the obvious version.

Reuse before adding. Extend what is already there. Introducing a new subsystem, a new dependency or a new pattern requires the user's agreement first - ask before writing it. A large change made without prior discussion is likely to be rejected.

Read before writing. Read the files a change touches and the files around them, then match the patterns in place.

Every source file starts with an `SPDX-License-Identifier: Apache-2.0` line. Code follows the Zephyr coding style.

## Git

### Commit messages

One line, in English, ASCII only. No unicode characters, no emoji. The text states briefly what was done and nothing else - no explanation, no list of files, no attribution or trailers.

The prefix drives automatic versioning:

| Commit message | Version increase |
|---|---|
| chore: foo | none |
| fix: fixed something | patch |
| feat: added new button to do X | minor |
| fix: fixed thing xyz + BREAKING CHANGE: paragraph | major |
| fix!: fixed something | major |
| feat!: added blah | major |

On every push to master, svu applies this table (.svu.yml) to the commits since the last tag and, when they raise the version, tags the pushed commit v<version>, which starts the release.

### Branches

Three kinds of branch exist:

- master - release state.
- develop - integration state.
- feature/<feature-name> - all work. Every task is done in its own feature branch, branched off develop.

### Merging

Subfeature into its feature branch: merge commit (git merge --no-ff), done here.

Feature into develop, and develop into master: done by the user through the external service. Never merge into develop or master from the command line.

### Pull requests

Never create a pull request. The user creates every pull request.

### Push

git push is forbidden unless the user asks for it explicitly. The request authorises one push. Later commits on the same feature require a new request each time.

### Examples

```
// GOOD: concise, one line, states what was done

fix: retry the request when the connection drops

// BAD: verbose, restates the change, adds a trailer

fix: comprehensive rework of connection retry handling

This commit introduces a comprehensive fix for the connection retry handling
logic, addressing an issue where a dropped connection could leave a request
without a response, thereby improving overall application reliability.

Co-authored-by: Claude Sonnet
```
