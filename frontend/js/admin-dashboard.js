// ============================================================================
//  admin-dashboard.js
//  Logic for admin-dashboard.html: admin auth guard, stats cards, and a
//  recent-elections table.
// ============================================================================

(async function init() {
    const user = Api.getUser();
    if (!user || !Api.getToken() || user.role !== "admin") {
        window.location.href = "login.html";
        return;
    }

    document.getElementById("welcomeHeading").textContent = "Welcome, " + user.fullName.split(" ")[0];

    document.getElementById("logoutBtn").addEventListener("click", async () => {
        try { await Api.post("/logout", {}); } catch (e) { /* ignore */ }
        Api.clearToken();
        window.location.href = "login.html";
    });

    await loadStats();
    await loadElections();

    async function loadStats() {
        const grid = document.getElementById("statsGrid");
        try {
            const res = await Api.get("/admin/stats");
            const s = res.data;

            const cards = [
                { label: "Total Voters", value: s.totalVoters, icon: usersIcon() },
                { label: "Verified Voters", value: s.verifiedVoters, icon: checkIcon() },
                { label: "Active Elections", value: s.activeElections, icon: ballotIcon() },
                { label: "Total Votes Cast", value: s.totalVotes, icon: chartIcon() }
            ];

            grid.innerHTML = cards.map((c) => `
                <div class="card stat-card">
                    <div class="stat-icon">${c.icon}</div>
                    <div class="stat-value">${c.value}</div>
                    <div class="stat-label">${c.label}</div>
                </div>
            `).join("");
        } catch (err) {
            grid.innerHTML = `<p style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</p>`;
        }
    }

    async function loadElections() {
        const tbody = document.getElementById("electionsTableBody");
        try {
            const res = await Api.get("/elections");
            const elections = res.data.slice(0, 8);

            if (elections.length === 0) {
                tbody.innerHTML = `<tr><td colspan="4" class="text-muted">No elections created yet.</td></tr>`;
                return;
            }

            tbody.innerHTML = elections.map((e) => `
                <tr>
                    <td>${UI.escapeHtml(e.title)}</td>
                    <td>${statusBadge(e.status)}</td>
                    <td class="text-muted">${UI.formatDate(e.startTime)} &rarr; ${UI.formatDate(e.endTime)}</td>
                    <td class="mono text-muted">${e.contractAddress ? UI.shortAddress(e.contractAddress) : "Not linked"}</td>
                </tr>
            `).join("");
        } catch (err) {
            tbody.innerHTML = `<tr><td colspan="4" style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</td></tr>`;
        }
    }

    function statusBadge(status) {
        const map = {
            active: ["badge-success", "Active"], upcoming: ["badge-warning", "Upcoming"],
            ended: ["badge-neutral", "Ended"], draft: ["badge-neutral", "Draft"], cancelled: ["badge-danger", "Cancelled"]
        };
        const [cls, label] = map[status] || ["badge-neutral", status];
        return `<span class="badge ${cls}"><span class="badge-dot"></span>${label}</span>`;
    }

    function usersIcon() { return `<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/></svg>`; }
    function checkIcon() { return `<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M8 12l3 3 5-6"/></svg>`; }
    function ballotIcon() { return `<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 12l2 2 4-4M12 3l8 4v5c0 5-3.5 9-8 10-4.5-1-8-5-8-10V7l8-4z"/></svg>`; }
    function chartIcon() { return `<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 3v18h18"/><path d="M7 14l4-4 3 3 5-6"/></svg>`; }
})();
