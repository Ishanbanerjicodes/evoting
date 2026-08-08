// ============================================================================
//  admin-audit.js
//  Logic for admin-audit.html: lists recent security-relevant events.
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

    const tbody = document.getElementById("auditTableBody");
    try {
        const res = await Api.get("/admin/audit-logs");
        const logs = res.data;

        if (logs.length === 0) {
            tbody.innerHTML = `<tr><td colspan="4" class="text-muted">No activity recorded yet.</td></tr>`;
            return;
        }

        tbody.innerHTML = logs.map((l) => `
            <tr>
                <td>${actionBadge(l.action)}</td>
                <td>${UI.escapeHtml(l.userFullName)}<br><span class="text-muted" style="font-size:var(--fs-xs);">${UI.escapeHtml(l.userEmail)}</span></td>
                <td class="text-soft" style="max-width:360px;">${UI.escapeHtml(l.details)}</td>
                <td class="text-muted mono" style="font-size:var(--fs-xs);">${UI.formatDate(l.createdAt)}</td>
            </tr>
        `).join("");
    } catch (err) {
        tbody.innerHTML = `<tr><td colspan="4" style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</td></tr>`;
    }

    function actionBadge(action) {
        const dangerActions = ["LOGIN_FAILED", "USER_STATUS_CHANGED", "ELECTION_DELETED"];
        const successActions = ["VOTE_CAST", "LOGIN_SUCCESS", "REGISTER"];
        let cls = "badge-neutral";
        if (dangerActions.includes(action)) cls = "badge-danger";
        else if (successActions.includes(action)) cls = "badge-success";
        else cls = "badge-accent";
        return `<span class="badge ${cls}"><span class="badge-dot"></span>${UI.escapeHtml(action)}</span>`;
    }
})();
