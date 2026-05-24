#!/bin/bash
# Clone one or more upstream BambuLab issues into BenJule/BambuStudio.
#
# Usage:
#   ./scripts/clone_upstream_issue.sh 1234
#   ./scripts/clone_upstream_issue.sh 1234 5678 9012
#
# Requirements: gh CLI (logged in with repo + issues scope), jq

set -euo pipefail

UPSTREAM="bambulab/BambuStudio"
FORK="BenJule/BambuStudio"
ASSIGNEE="BenJule"
MILESTONE=2   # v02.07.00-dev

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <issue-number> [issue-number ...]" >&2
    exit 1
fi

for NUM in "$@"; do
    echo "Fetching upstream #${NUM}..."

    DATA=$(gh issue view "$NUM" --repo "$UPSTREAM" \
        --json number,title,body,labels,url 2>/dev/null) || {
        echo "  ✗ Issue #${NUM} not found on upstream" >&2
        continue
    }

    TITLE=$(echo "$DATA" | jq -r '.title')
    BODY=$(echo "$DATA"  | jq -r '.body // ""')
    URL=$(echo "$DATA"   | jq -r '.url')
    LABELS=$(echo "$DATA" | jq -r '[.labels[].name] | join(", ")')

    FULL_BODY="**Upstream issue:** ${URL}
**Labels upstream:** ${LABELS:-–}

---

${BODY}"

    CREATED=$(gh issue create \
        --repo "$FORK" \
        --title "${TITLE}" \
        --body "$FULL_BODY" \
        --assignee "$ASSIGNEE" 2>&1)

    # Assign milestone separately (gh issue create has no --milestone <id> flag)
    ISSUE_NUM="${CREATED##*/issues/}"
    if [[ "$ISSUE_NUM" =~ ^[0-9]+$ ]]; then
        gh api "repos/${FORK}/issues/${ISSUE_NUM}" \
            -X PATCH -f milestone="$MILESTONE" --silent
        echo "  ✓ Created #${ISSUE_NUM}: ${TITLE}"
        echo "    ${CREATED}"
    else
        echo "  ✗ Could not parse issue number from: ${CREATED}" >&2
    fi
done
