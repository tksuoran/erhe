---
name: git-history-cleanup
description: Collapse many WIP / fix-to-fix commits on a feature branch into a small set of topic-focused commits. Reads the original commit messages first so intent is preserved, soft-resets to origin/main so intermediate / overwritten changes drop out automatically, then re-commits the accumulated diff one logical topic at a time. Use before opening a PR or merging when `git log origin/main..HEAD` looks noisy.
---

# Git history cleanup

Collapse `origin/main..HEAD` into a small number of topic-focused commits without losing intent. Changes that were introduced in one commit and rewritten or undone by a later commit drop out automatically -- they are not in the final diff. The original commit messages are read first so the new topic split preserves the *why* of the work.

The previous tip of the branch is kept by `git reflog` (default 90 days), so the operation is non-destructive locally. Nothing is pushed.

## Step 0 -- working copy must be clean

```sh
git status --porcelain
```

If the output is non-empty, STOP. Tell the user to commit, stash, or discard their pending edits first. A dirty tree would mix uncommitted work into the collapse and there is no clean way to un-mix afterwards. Do not improvise an auto-stash; the user may not want those edits in the regrouped history.

## Step 1 -- agent context must be clean

This task reads every commit message between `origin/main` and `HEAD`, plus the full accumulated diff, plus the per-file change set. That is heavy and benefits from a fresh context window so nothing important gets truncated mid-analysis.

Self-assess:

- **Clean context** -- this skill was invoked early in the conversation, with little prior tool use: proceed in-place.
- **Dirty context** -- there has been substantial prior work (many file reads, prior edits, long tool history): spawn a `general-purpose` Agent and hand off steps 2-6 with a self-contained brief. The brief MUST include:
  - the absolute repo path,
  - the current branch name,
  - the `origin/main` SHA (`git rev-parse origin/main`),
  - the explicit rules from "Hard rules" below (no push, no `--hard`, no `-i`, no `--no-verify`),
  - an instruction to surface the topic plan back to the user for confirmation before any commit.

When in doubt, prefer spawning the agent; the overhead is small and the failure mode of running out of context mid-analysis is bad (a half-grouped history with no record of the original intent).

## Step 2 -- study the existing commits

Read full bodies, not just subjects:

```sh
git log origin/main..HEAD --pretty=format:'%n=== %h %an %ad%n%s%n%n%b'
git log origin/main..HEAD --name-status
```

For each commit capture:

- Stated intent (what the message says it did, and why).
- Whether it fixes / reverts / amends a previous commit on the same branch -- these are the "overwritten" changes the user wants to forget. They will not appear in the final diff but their messages still hint at the *topic* the eventual hunk belongs to.
- Which files / areas it touched.

Save the output, or at least your notes from it, somewhere reachable in step 4 -- once the reset happens these messages are still in the reflog but reading them again costs another pass.

Also useful when the branch is long or branchy:

```sh
git log origin/main..HEAD --merges            # are there merge commits in the range?
git log origin/main..HEAD --reverse --oneline # chronological order, easier for narrative
```

If there are merge commits, surface them to the user and ask whether to flatten -- this skill assumes a linear range.

## Step 3 -- collapse to origin/main

The user's phrasing "soft reset" here means "non-destructive reset that preserves all changes". Use `--mixed` (the git default) so everything lands as **unstaged** working-tree changes -- that is what step 4 analyzes and step 5 selectively stages per topic:

```sh
git reset --mixed origin/main
```

Equivalent two-step that honors the literal "soft" wording:

```sh
git reset --soft origin/main
git reset HEAD --
```

After either form:

- `HEAD` is at `origin/main`.
- The working tree is unchanged.
- `git diff` shows only the *final combined effect* of all the original commits. Intermediate / overwritten states are automatically gone.

Do NOT use `git reset --hard` here. `--hard` discards the working tree, which erases the entire point of the operation.

## Step 4 -- group the diff by topic

```sh
git status
git diff --stat
git diff
```

Cluster the changes into logical topics:

- Re-read the commit messages from step 2 -- they encode the user's intended topics. Subjects that share a theme ("fix multiview", "fix multiview alignment", "follow-up to multiview") usually collapse into one topic.
- Numbered punch-list prefixes (e.g. `A11-A20`, `B1/B6/B10`, `E1/E13`) often map directly onto the original task tracker's section -- preserve those groupings unless the diff says otherwise.
- A topic is "one logical change a reviewer can approve or revert as a unit". Aim for 2-6 topics on a typical feature branch; fewer is better as long as each remains coherent.
- If a single file has hunks belonging to different topics, plan for hunk-level staging in step 5.

Present the proposed split to the user before committing anything:

- Each topic: a draft subject line, optional body, and the list of files (or files-and-hunks for mixed files) it covers.
- Total file count covered vs. `git diff --stat` total -- they must match.

Wait for the user to confirm or redirect. Do NOT silently regroup.

## Step 5 -- commit each topic

Before the first commit, snapshot the pre-commit diff so step 6 can verify nothing was dropped:

```sh
git diff > /tmp/git-history-cleanup-target.diff
```

Then, for each topic in order:

1. Stage exactly that topic:
   ```sh
   git add path/to/whole_topic_files
   git add -p path/to/mixed_file        # interactive; only when a file has mixed-topic hunks
   ```
   When `git add -p` is awkward to drive non-interactively, write per-hunk patches to disk and apply with `git apply --cached <hunk.patch>`. Do not paper over difficulty by lumping a mixed file into one topic if it really belongs to two -- that defeats the purpose of the skill.

2. Sanity check before committing:
   ```sh
   git diff --cached    # should show ONLY this topic
   git diff             # should show everything still pending for later topics
   ```

3. Commit using the project's style. For erhe (see AGENTS.md "Git Workflow"):
   - Short subject, ideally under ~70 chars, focused on the *why*.
   - Optional body for context.
   - Add an AI co-author trailer only when the user or repository policy explicitly requires one; do not invent a model-specific identity.
   - Use a HEREDOC for multi-line messages.

4. Move on to the next topic.

## Step 6 -- verify and stop

After the last topic commit, BOTH of these must hold:

```sh
git status                          # must be clean -- no remaining changes
git diff origin/main..HEAD > /tmp/git-history-cleanup-after.diff
diff /tmp/git-history-cleanup-target.diff /tmp/git-history-cleanup-after.diff
```

The `diff` must be empty (or only differ in trailing whitespace). Equivalently, the total diff vs. `origin/main` must equal the diff captured before any topic commits.

If either check fails a hunk was dropped or duplicated. DO NOT rescue with a catch-all "misc" / "wip" commit -- that defeats the purpose. Find the missing hunk, decide which topic it belongs to, and either amend that topic's commit (only if it is the most recent one) or do a fresh `git reset --mixed origin/main` and re-run from step 5.

Show the user the new shortlog and stop:

```sh
git log origin/main..HEAD --oneline
```

Pushing is the user's call.

## Hard rules

- **Never `git push`** -- and especially never `git push --force` -- from this skill. The user decides when the new history goes out.
- **Never `git reset --hard`**. The whole operation depends on the working tree being preserved by the reset.
- **Never `git rebase -i`**. Interactive prompts cannot be driven from this tool.
- **Never `--no-verify`, `--no-gpg-sign`**, or other hook / signing bypasses.
- **Never create a "misc" / "wip" / "cleanup" catch-all commit** to swallow leftover changes -- that defeats the purpose of the skill. Either every hunk belongs to a named topic, or step 6 must surface the gap to the user.
- **Never amend a commit that was not created by this skill run**. Amending commits the user already pushed elsewhere is destructive.
- If any check (step 0 clean tree, step 6 diff equality) fails, STOP and report. Do not improvise.

## Recovery

If the user dislikes the new history, the previous tip is in the reflog:

```sh
git reflog | head -30
git reset --hard <old-tip-sha>
```

When reporting completion to the user, mention the old tip SHA explicitly (capture it from `git rev-parse HEAD` BEFORE the step-3 reset) so they know the safety net is one command away.

## When NOT to use

- Branch has merge commits in `origin/main..HEAD` that the user wants to preserve -- this skill assumes a linear range and would flatten the merge boundary.
- Branch has already been pushed and other people are working from it -- the resulting force-push would rewrite shared history. Confirm with the user that the branch is private before starting.
- There is no meaningful diff (`git log origin/main..HEAD --oneline | wc -l` is 0 or 1, or `git diff origin/main..HEAD` is empty) -- there is nothing to clean up. Tell the user.
