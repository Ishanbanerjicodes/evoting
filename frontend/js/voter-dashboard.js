// ============================================================================
//  voter-dashboard.js
//  Logic for voter-dashboard.html: auth guard, election listing, wallet
//  connect button.
// ============================================================================

(async function init() {
    const user = Api.getUser();
    if (!user || !Api.getToken()) {
        window.location.href = "login.html";
        return;
    }

    document.getElementById("welcomeHeading").textContent = "Welcome back, " + user.fullName.split(" ")[0];

    document.getElementById("logoutBtn").addEventListener("click", async () => {
        try { await Api.post("/logout", {}); } catch (e) { /* ignore */ }
        Api.clearToken();
        window.location.href = "login.html";
    });

    // ---- Wallet connect --------------------------------------------------
    const connectBtn = document.getElementById("connectWalletBtn");
    const chipContainer = document.getElementById("walletChipContainer");

    function renderWalletChip(address) {
        chipContainer.innerHTML = `<span class="wallet-chip"><span class="dot"></span> ${UI.shortAddress(address)}</span>`;
        connectBtn.style.display = "none";
    }

    if (user.walletAddress) {
        renderWalletChip(user.walletAddress);
    }

    connectBtn.addEventListener("click", async () => {
        UI.setLoading(connectBtn, true, "Connecting…");
        try {
            const address = await Wallet.connect();
            await Api.post("/link-wallet", { walletAddress: address });
            renderWalletChip(address);
            UI.toast("Wallet connected successfully", "success");
        } catch (err) {
            UI.toast(err.message, "error");
        } finally {
            UI.setLoading(connectBtn, false);
        }
    });

    // ---- Load elections --------------------------------------------------
    await loadElections();

    async function loadElections() {
        const grid = document.getElementById("electionsGrid");
        try {
            const res = await Api.get("/elections");
            const elections = res.data;

            if (elections.length === 0) {
                grid.innerHTML = `<div class="empty-state" style="grid-column:1/-1;">
                    <div class="empty-icon"><svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2"/></svg></div>
                    <h4>No elections yet</h4>
                    <p>Check back once the administrator schedules an election.</p>
                </div>`;
                return;
            }

            grid.innerHTML = elections.map(renderElectionCard).join("");
        } catch (err) {
            grid.innerHTML = `<div class="empty-state" style="grid-column:1/-1;">
                <p style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</p>
            </div>`;
        }
    }

    function statusBadge(status) {
        const map = {
            active: ["badge-success", "Live now"],
            upcoming: ["badge-warning", "Upcoming"],
            ended: ["badge-neutral", "Ended"],
            draft: ["badge-neutral", "Draft"],
            cancelled: ["badge-danger", "Cancelled"]
        };
        const [cls, label] = map[status] || ["badge-neutral", status];
        return `<span class="badge ${cls}"><span class="badge-dot"></span>${label}</span>`;
    }

    function renderElectionCard(e) {
        let actionHtml;
        if (e.status === "active") {
            actionHtml = `<a href="vote.html?electionId=${e.electionId}" class="btn btn-primary btn-block">Vote Now</a>`;
        } else if (e.status === "ended") {
            actionHtml = `<a href="results.html?electionId=${e.electionId}" class="btn btn-secondary btn-block">View Results</a>`;
        } else {
            actionHtml = `<button class="btn btn-secondary btn-block" disabled>Not Open Yet</button>`;
        }

        return `
        <div class="card animate-fade-up">
            <div style="display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:12px;">
                ${statusBadge(e.status)}
            </div>
            <h4>${UI.escapeHtml(e.title)}</h4>
            <p class="text-soft" style="min-height:44px;">${UI.escapeHtml(e.description || "No description provided.")}</p>
            <p class="text-muted" style="font-size:var(--fs-xs); margin-bottom:20px;">
                ${UI.formatDate(e.startTime)} &rarr; ${UI.formatDate(e.endTime)}
            </p>
            ${actionHtml}
        </div>`;
    }
})();
