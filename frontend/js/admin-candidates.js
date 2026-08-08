// ============================================================================
//  admin-candidates.js
//  Logic for admin-candidates.html: pick an election, manage its candidates.
// ============================================================================

(async function init() {
    const user = Api.getUser();
    if (!user || !Api.getToken() || user.role !== "admin") {
        window.location.href = "login.html";
        return;
    }

    document.getElementById("logoutBtn").addEventListener("click", async () => {
        try { await Api.post("/logout", {}); } catch (e) { /* ignore */ }
        Api.clearToken();
        window.location.href = "login.html";
    });

    const electionSelect = document.getElementById("electionSelect");
    let currentElectionId = null;

    await loadElectionOptions();

    electionSelect.addEventListener("change", () => {
        currentElectionId = electionSelect.value;
        loadCandidates();
    });

    async function loadElectionOptions() {
        try {
            const res = await Api.get("/elections");
            const elections = res.data;

            if (elections.length === 0) {
                electionSelect.innerHTML = `<option>No elections created yet</option>`;
                document.getElementById("candidatesGrid").innerHTML =
                    `<p class="text-muted">Create an election first, then add candidates to it.</p>`;
                document.getElementById("newCandidateBtn").disabled = true;
                return;
            }

            electionSelect.innerHTML = elections.map((e) =>
                `<option value="${e.electionId}">${UI.escapeHtml(e.title)}</option>`
            ).join("");

            currentElectionId = elections[0].electionId;
            await loadCandidates();
        } catch (err) {
            UI.toast(err.message, "error");
        }
    }

    async function loadCandidates() {
        const grid = document.getElementById("candidatesGrid");
        grid.innerHTML = `<div class="card skeleton" style="height:200px;"></div>`;

        try {
            const res = await Api.get(`/candidates?electionId=${currentElectionId}`);
            const candidates = res.data;

            if (candidates.length === 0) {
                grid.innerHTML = `<div class="empty-state" style="grid-column:1/-1;"><p>No candidates added to this election yet.</p></div>`;
                return;
            }

            grid.innerHTML = candidates.map((c) => `
                <div class="card">
                    <div class="candidate-avatar" style="margin-bottom:12px;">${UI.initials(c.fullName)}</div>
                    <h4 style="margin-bottom:2px;">${UI.escapeHtml(c.fullName)}</h4>
                    <p class="text-muted" style="font-size:var(--fs-sm); margin-bottom:8px;">${UI.escapeHtml(c.partyName || "Independent")}</p>
                    <p class="text-soft" style="font-size:var(--fs-sm);">${UI.escapeHtml(c.bio || "")}</p>
                    <div style="display:flex; justify-content:space-between; align-items:center; margin-top:16px;">
                        <span class="badge badge-accent"><span class="badge-dot"></span>${c.voteCount} votes</span>
                        <span class="text-muted mono" style="font-size:var(--fs-xs);">chain idx: ${c.onChainIndex}</span>
                    </div>
                </div>
            `).join("");
        } catch (err) {
            grid.innerHTML = `<p style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</p>`;
        }
    }

    document.getElementById("newCandidateBtn").addEventListener("click", () => {
        document.getElementById("candidateForm").reset();
        document.getElementById("candidateModal").showModal();
    });
    document.getElementById("cancelCandidateBtn").addEventListener("click", () => {
        document.getElementById("candidateModal").close();
    });

    document.getElementById("candidateForm").addEventListener("submit", async (e) => {
        e.preventDefault();
        const btn = document.getElementById("saveCandidateBtn");
        UI.setLoading(btn, true, "Saving…");

        try {
            await Api.post("/candidates", {
                electionId: parseInt(currentElectionId, 10),
                fullName: document.getElementById("fullNameInput").value.trim(),
                partyName: document.getElementById("partyNameInput").value.trim(),
                bio: document.getElementById("bioInput").value.trim(),
                onChainIndex: parseInt(document.getElementById("onChainIndexInput").value, 10)
            });

            UI.toast("Candidate added", "success");
            document.getElementById("candidateModal").close();
            await loadCandidates();
        } catch (err) {
            UI.toast(err.message, "error");
        } finally {
            UI.setLoading(btn, false);
        }
    });
})();
