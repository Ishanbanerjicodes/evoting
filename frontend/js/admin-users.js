// ============================================================================
//  admin-users.js
//  Logic for admin-users.html: list registered voters, toggle active status.
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

    await loadUsers();

    async function loadUsers() {
        const tbody = document.getElementById("usersTableBody");
        try {
            const res = await Api.get("/admin/users");
            const users = res.data.filter((u) => u.role === "voter");

            if (users.length === 0) {
                tbody.innerHTML = `<tr><td colspan="6" class="text-muted">No voters have registered yet.</td></tr>`;
                return;
            }

            tbody.innerHTML = users.map((u) => `
                <tr>
                    <td>${UI.escapeHtml(u.fullName)}</td>
                    <td class="text-muted">${UI.escapeHtml(u.email)}</td>
                    <td class="mono text-muted">${UI.escapeHtml(u.voterIdNumber)}</td>
                    <td class="mono text-muted">${u.walletAddress ? UI.shortAddress(u.walletAddress) : "Not linked"}</td>
                    <td>${u.isActive
                        ? `<span class="badge badge-success"><span class="badge-dot"></span>Active</span>`
                        : `<span class="badge badge-danger"><span class="badge-dot"></span>Disabled</span>`}
                    </td>
                    <td>
                        <button class="btn btn-sm ${u.isActive ? 'btn-danger' : 'btn-secondary'} toggle-btn"
                                data-id="${u.userId}" data-active="${u.isActive}">
                            ${u.isActive ? 'Disable' : 'Enable'}
                        </button>
                    </td>
                </tr>
            `).join("");

            tbody.querySelectorAll(".toggle-btn").forEach((btn) => {
                btn.addEventListener("click", async () => {
                    const isCurrentlyActive = btn.dataset.active === "true";
                    const confirmed = await UI.confirmDialog(
                        isCurrentlyActive
                            ? "This voter will no longer be able to log in or vote."
                            : "This voter will regain access to log in and vote.",
                        isCurrentlyActive ? "Disable this voter?" : "Enable this voter?"
                    );
                    if (!confirmed) return;

                    try {
                        await Api.put(`/admin/users/${btn.dataset.id}/status`, { isActive: !isCurrentlyActive });
                        UI.toast("Voter status updated", "success");
                        await loadUsers();
                    } catch (err) {
                        UI.toast(err.message, "error");
                    }
                });
            });
        } catch (err) {
            tbody.innerHTML = `<tr><td colspan="6" style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</td></tr>`;
        }
    }
})();
