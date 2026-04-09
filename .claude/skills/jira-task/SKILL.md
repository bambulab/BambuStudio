---
name: jira-task
description: Fetch a Jira ticket, research the codebase, plan the implementation, and execute it. Use when the user provides a Jira ticket URL, key (STUDIO-15807), or bare number (15807).
---

# Jira Task Processor

End-to-end workflow: fetch a Jira ticket → research the codebase → plan → implement.

Intermediate files are saved to `.jira/{JIRA_KEY}/` (e.g. `.jira/STUDIO-15807/`).

## Input Parsing

The user provides the ticket as `$ARGUMENTS`. Parse it into a Jira issue key:

| User input | Parsed key |
|---|---|
| `https://<jira-host>/browse/STUDIO-15807` | `STUDIO-15807` |
| `STUDIO-15807` | `STUDIO-15807` |
| `15807` | `STUDIO-15807` |

Rules:
- If the input is a URL, extract the key from the path after `/browse/`
- If the input matches `[A-Z]+-\d+`, use it directly
- If the input is a bare number, prefix with `STUDIO-`

Store the parsed key as `JIRA_KEY` (e.g. `STUDIO-15807`).

## Step 1: Check Credentials

Look for the file `.jira/.credentials` in the project root.

If it exists, read it. It contains:
```
JIRA_URL=https://jira.example.com
JIRA_USERNAME=user@example.com
JIRA_TOKEN=the-api-token
JIRA_AUTH=Bearer
```

If it does **not** exist, or if `JIRA_URL` is missing from the file:
1. Ask the user for their Jira base URL (e.g. `https://jira.example.com`), username, and API token using AskUserQuestion
2. Create the `.jira/` directory and write `.jira/.credentials` with the provided values
3. Ensure `.jira/` is in `.gitignore` (add it if missing)

Load the URL, username, and token into variables for subsequent API calls. Store the base URL as `JIRA_URL` (no trailing slash).

## Step 2: Fetch Jira Ticket

Call the Jira REST API to get the ticket details. The authentication method depends on `JIRA_AUTH`:

- If `JIRA_AUTH=Bearer`: use `-H "Authorization: Bearer $JIRA_TOKEN"`
- If `JIRA_AUTH=Basic` or not set: use `-u "$JIRA_USERNAME:$JIRA_TOKEN"`

```bash
curl -s -H "Authorization: Bearer $JIRA_TOKEN" \
  "$JIRA_URL/rest/api/2/issue/$JIRA_KEY" \
  | python -m json.tool
```

If this returns a 401, tell the user their credentials are invalid and ask them to update `.jira/.credentials`.

If this returns a 404, tell the user the ticket was not found.

From the JSON response, extract:
- `fields.summary` — ticket title
- `fields.description` — full description
- `fields.issuetype.name` — Bug, Story, Task, etc.
- `fields.status.name` — current status
- `fields.priority.name` — priority level
- `fields.assignee.displayName` — who it's assigned to
- `fields.labels` — labels
- `fields.components` — components
- `fields.comment.comments[]` — discussion comments (author + body)
- `fields.subtasks[]` — subtask keys and summaries
- `fields.issuelinks[]` — linked issues

## Step 3: Save Ticket Details

Create the directory `.jira/$JIRA_KEY/` if it doesn't exist.

Write `.jira/$JIRA_KEY/ticket.md` with the extracted ticket information, formatted as:

```markdown
# {JIRA_KEY}: {summary}

- **Type**: {issuetype}
- **Status**: {status}
- **Priority**: {priority}
- **Assignee**: {assignee}
- **Labels**: {labels}
- **Components**: {components}
- **URL**: {JIRA_URL}/browse/{JIRA_KEY}

## Description

{description}

## Comments

### {comment_author} — {comment_date}
{comment_body}

## Subtasks

- {subtask_key}: {subtask_summary}

## Linked Issues

- {link_type}: {linked_issue_key} — {linked_issue_summary}
```

Omit empty sections.

## Step 4: Research Phase

Based on the ticket description and requirements, explore the codebase to understand:
- Which files and modules are relevant to this ticket
- Existing patterns, utilities, or similar implementations that can be reused
- Architecture and dependencies in the affected area
- Any tests that cover the affected area

**For Bug tickets**: Additionally investigate and document:
- **Root cause**: Trace the code path that leads to the bug. Identify the exact function(s) and condition(s) where the incorrect behavior originates
- **Error code path**: Document the full call chain from entry point to the point of failure, including relevant function signatures and file locations
- **Reproduction logic**: Describe how the inputs/state lead to the erroneous behavior based on the code
- **Related fixes**: Check git log for previous fixes in the same area that might provide context

Use the Explore agent or Glob/Grep tools to search the codebase thoroughly.

Save findings to `.jira/$JIRA_KEY/research.md` with the structure:

```markdown
# Research: {JIRA_KEY}

## Relevant Files

- `path/to/file.cpp` — description of what it does and why it's relevant

## Existing Patterns

- Description of relevant patterns found in the codebase

## Architecture Notes

- How the affected components are structured and connected

## Root Cause Analysis (for bugs)

### Error Code Path
1. `entry_function()` in `path/to/file.cpp:123`
2. → calls `intermediate_function()` in `path/to/other.cpp:456`
3. → bug occurs at `faulty_function()` in `path/to/bug.cpp:789` because {reason}

### Root Cause
{Detailed explanation of why the bug occurs}

### Reproduction Logic
{How the inputs/state trigger the bug through the code path above}

## Test Coverage

- Existing tests that cover the affected area
```

Omit the "Root Cause Analysis" section for non-bug tickets.

## Step 5: Plan Phase

Based on the ticket details and research, create an implementation plan.

Save the plan to `.jira/$JIRA_KEY/plan.md` with the structure:

```markdown
# Plan: {JIRA_KEY} — {summary}

## Approach

{High-level description of the implementation approach}

## Changes

### 1. {file_path}
- What to change and why

### 2. {file_path}
- What to change and why

## Testing

- How to verify the changes work correctly

## Risks

- Any risks or edge cases to watch out for
```

**Present the plan to the user and wait for approval before proceeding.** Use AskUserQuestion or simply present the plan and ask if they want to proceed. Do NOT implement without explicit approval.

## Step 6: Implement

Once the user approves the plan, execute it:
1. Make the code changes described in the plan
2. After implementation, briefly summarize what was done

## Step 7: Commit and Push to Gerrit

After the user accepts the implementation (explicitly or implicitly), create a commit and push it to Gerrit for code review.

### Commit message format

Follow the project's existing commit message convention. The message has two lines separated by a blank line:

```
{TYPE}: {description}

JIRA: {JIRA_KEY}
```

Where `{TYPE}` is derived from the ticket type:
- Bug → `FIX`
- Story/Feature → `ENH`
- Task → `ENH` (or `FIX` if the task is a fix)
- Refactor → `REFACTOR`

The `JIRA: {JIRA_KEY}` line MUST be on its own line after the description, separated by a blank line.

Examples:
```
FIX: show error dialog when gcode.3mf export fails due to locked file

JIRA: STUDIO-17233
Co-Authored-By: Claude Opus 4 <noreply@anthropic.com>
```

```
ENH: add multi-nozzle support for X2D

JIRA: STUDIO-15807
Co-Authored-By: Claude Opus 4 <noreply@anthropic.com>
```

Always include the `Co-Authored-By: Claude Opus 4 <noreply@anthropic.com>` line after `JIRA: {JIRA_KEY}`.

### Commit steps

1. Stage only the files you modified (do NOT use `git add -A` or `git add .`)
2. Create the commit. The Gerrit commit-msg hook will automatically add a `Change-Id` trailer.
3. Verify the commit was created successfully with `git log -1`

### Push to Gerrit

Push the commit to Gerrit for review:

```bash
git push origin HEAD:refs/for/master
```

If the current branch is not `master`, push to the appropriate target branch:

```bash
git push origin HEAD:refs/for/{target_branch}
```

After pushing, report the Gerrit review URL to the user (it is printed in the push output).

### Error handling for push

| Error | Action |
|---|---|
| `no new changes` | The change already exists in Gerrit — inform the user |
| `permission denied` | Ask user to check SSH keys / Gerrit access |
| `hook` errors | The commit-msg hook may have failed — check if Change-Id was added |

## Error Handling

| Error | Likely Cause | Action |
|---|---|---|
| 401 Unauthorized | Wrong email/token | Ask user to check `.jira/.credentials` |
| 404 Not Found | Wrong ticket key | Verify the key, check if project prefix is correct |
| Connection error | VPN/network issue | Ask user to check network access to the Jira host configured in `.jira/.credentials` |
| Empty `$ARGUMENTS` | User didn't provide a ticket | Ask user to provide a ticket number |

## Tips

- If `$ARGUMENTS` is empty, ask the user which ticket to work on
- For large tickets with many subtasks, ask the user which subtask to focus on
- If the ticket description is vague, check comments for additional context
- Always check if there's an existing branch or PR for this ticket before starting work
