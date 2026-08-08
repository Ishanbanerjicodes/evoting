// ============================================================================
//  results.js
//  Logic for results.html — public page, no login required. Lets the
//  visitor pick any election and see live, animated vote tallies.
// ============================================================================

(async function init() {
    const params = new URLSearchParams(window.location.search);
    const preselectedId = params.get("electionId");

    const select = document.getElementById("electionSelect");

    try {
        const res = await Api.get("/elections");
        const elections = res.data;

        if (elections.length === 0) {
            document.getElementById("selectorWrap").style.display = "none";
            document.getElementById("resultsContent").innerHTML = emptyState("No elections found.");
            return;
        }

        select.innerHTML = elections.map((e) =>
            `<option value="${e.electionId}">${UI.escapeHtml(e.title)} (${e.status})</option>`
        ).join("");

        if (preselectedId && elections.some((e) => String(e.electionId) === preselectedId)) {
            select.value = preselectedId;
        }

        select.addEventListener("change", () => loadResults(select.value));
        await loadResults(select.value);
    } catch (err) {
        document.getElementById("resultsContent").innerHTML =
            `<p style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</p>`;
    }

    async function loadResults(electionId) {
        const container = document.getElementById("resultsContent");
        container.innerHTML = `<div class="card skeleton" style="height:220px;"></div>`;

        try {
            const res = await Api.get(`/results?electionId=${electionId}`);
            const data = res.data;

            if (data.candidates.length === 0) {
                container.innerHTML = emptyState("No candidates in this election yet.");
                return;
            }

            const maxVotes = Math.max(...data.candidates.map((c) => c.voteCount), 1);

            container.innerHTML = `
                <div class="card">
                    <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:24px;">
                        <h4 style="margin-bottom:0;">Total votes cast</h4>
                        <span class="badge badge-accent"><span class="badge-dot"></span>${data.totalVotes}</span>
                    </div>
                    <div style="display:flex; flex-direction:column; gap:22px;">
                        ${data.candidates.map((c, i) => resultRow(c, i, maxVotes)).join("")}
                    </div>
                </div>`;

            // Animate bars in after paint.
            requestAnimationFrame(() => {
                container.querySelectorAll(".result-bar-fill").forEach((bar) => {
                    bar.style.width = bar.dataset.targetWidth;
                });
            });
        } catch (err) {
            container.innerHTML = `<p style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</p>`;
        }
    }

    function resultRow(candidate, index, maxVotes) {
        const widthPct = maxVotes > 0 ? (candidate.voteCount / maxVotes) * 100 : 0;
        const isLeading = index === 0 && candidate.voteCount > 0;
        return `
            <div>
                <div style="display:flex; justify-content:space-between; margin-bottom:8px;">
                    <span style="font-weight:600;">
                        ${UI.escapeHtml(candidate.fullName)}
                        ${isLeading ? '<span class="badge badge-success" style="margin-left:8px;"><span class="badge-dot"></span>Leading</span>' : ''}
                    </span>
                    <span class="text-muted mono">${candidate.voteCount} votes (${candidate.votePercentage || 0}%)</span>
                </div>
                <div class="result-bar-track">
                    <div class="result-bar-fill" data-target-width="${widthPct}%"></div>
                </div>
            </div>`;
    }

    function emptyState(message) {
        return `<div class="empty-state">
            <div class="empty-icon"><svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 3v18h18"/></svg></div>
            <p>${UI.escapeHtml(message)}</p>
        </div>`;
    }
})();
