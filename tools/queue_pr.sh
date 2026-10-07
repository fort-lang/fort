#!/bin/bash
# tools/queue_pr.sh <number> <reviewed-head-sha>: add a PR to the merge queue of main.
#
# `gh pr merge` cannot queue a PR in this repository. It enables auto-merge, and the repository
# disables auto-merge. So this script runs the GraphQL mutation enqueuePullRequest.
# Its expectedHeadOid is the reviewed head SHA, so GitHub refuses the PR if its head moved.
# The script prints the queue position. It exits 1 on a GraphQL error or a missing position.
# It never merges and never pushes; the queue merges the PR.
set -eu

usage() {
    echo "usage: queue_pr.sh <number> <reviewed-head-sha>" >&2
}

if [ "$#" -ne 2 ]; then
    usage
    exit 2
fi
number=$1
sha=$2
if ! [[ $number =~ ^[0-9]+$ ]]; then
    echo "queue_pr.sh: '$number' is not a PR number" >&2
    usage
    exit 2
fi
if ! [[ $sha =~ ^[0-9a-f]{40}$ ]]; then
    echo "queue_pr.sh: '$sha' is not a full 40-digit head SHA" >&2
    usage
    exit 2
fi

if ! id=$(gh pr view "$number" --json id --jq .id); then
    echo "queue_pr.sh: cannot read the node ID of PR $number" >&2
    exit 1
fi

# The single quotes keep $id and $oid for GraphQL, not for the shell.
# shellcheck disable=SC2016
query='mutation($id: ID!, $oid: GitObjectID!) {
  enqueuePullRequest(input: {pullRequestId: $id, expectedHeadOid: $oid}) {
    mergeQueueEntry { position }
  }
}'
# gh api exits non-zero and prints the GraphQL errors when the response holds any.
if ! position=$(gh api graphql -f query="$query" -f id="$id" -f oid="$sha" \
    --jq '.data.enqueuePullRequest.mergeQueueEntry.position'); then
    echo "queue_pr.sh: GitHub refused to queue PR $number at $sha" >&2
    exit 1
fi
if ! [[ $position =~ ^[0-9]+$ ]]; then
    echo "queue_pr.sh: GitHub returned no queue position for PR $number: '$position'" >&2
    exit 1
fi
echo "PR $number at $sha is in the merge queue at position $position"
