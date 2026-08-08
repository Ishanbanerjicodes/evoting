// ============================================================================
//  admin-elections.js
//  Logic for admin-elections.html: create elections and update their
//  status / contract address once deployed.
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

    let editingElectionId = null;

    await loadElections();

    document.getElementById("newElectionBtn").addEventListener("click", () => openModal());
    document.getElementById("cancelElectionBtn").addEventListener("click", closeModal);

    function openModal(election) {
        editingElectionId = election ? election.electionId : null;
        document.getElementById("modalMode").textContent = election ? "Edit election" : "New election";

        document.getElementById("titleInput").value = election ? election.title : "";
        document.getElementById("descriptionInput").value = election ? election.description : "";
        document.getElementById("startTimeInput").value = election ? toDatetimeLocal(election.startTime) : "";
        document.getElementById("endTimeInput").value = election ? toDatetimeLocal(election.endTime) : "";
        document.getElementById("contractAddressInput").value = election ? election.contractAddress : "";
        document.getElementById("statusInput").value = election ? election.status : "draft";

        // Title/start/end are only settable at creation time in this build
        // (the backend's update endpoint focuses on status + contract
        // address, matching real election-admin workflows where the core
        // details are locked once an election exists).
        const lockable = !!election;
        document.getElementById("titleInput").disabled = lockable;
        document.getElementById("descriptionInput").disabled = lockable;
        document.getElementById("startTimeInput").disabled = lockable;
        document.getElementById("endTimeInput").disabled = lockable;

        document.getElementById("electionModal").showModal();
    }

    function closeModal() {
        document.getElementById("electionModal").close();
        document.getElementById("electionForm").reset();
    }

    function toDatetimeLocal(mysqlDatetime) {
        // MySQL DATETIME comes back as "YYYY-MM-DD HH:MM:SS" — convert to
        // the "YYYY-MM-DDTHH:MM" shape <input type="datetime-local"> needs.
        if (!mysqlDatetime) return "";
        return mysqlDatetime.replace(" ", "T").slice(0, 16);
    }

    document.getElementById("electionForm").addEventListener("submit", async (e) => {
        e.preventDefault();
        const btn = document.getElementById("saveElectionBtn");
        UI.setLoading(btn, true, "Saving…");

        try {
            if (editingElectionId) {
                await Api.put(`/elections/${editingElectionId}`, {
                    status: document.getElementById("statusInput").value,
                    contractAddress: document.getElementById("contractAddressInput").value.trim()
                });
                UI.toast("Election updated", "success");
            } else {
                const title = document.getElementById("titleInput").value.trim();
                const startTime = document.getElementById("startTimeInput").value;
                const endTime = document.getElementById("endTimeInput").value;

                if (!title || !startTime || !endTime) {
                    UI.toast("Title, start time and end time are required", "error");
                    UI.setLoading(btn, false);
                    return;
                }

                await Api.post("/elections", {
                    title,
                    description: document.getElementById("descriptionInput").value.trim(),
                    startTime: startTime.replace("T", " ") + ":00",
                    endTime: endTime.replace("T", " ") + ":00",
                    contractAddress: document.getElementById("contractAddressInput").value.trim()
                });
                UI.toast("Election created", "success");
            }

            closeModal();
            await loadElections();
        } catch (err) {
            UI.toast(err.message, "error");
        } finally {
            UI.setLoading(btn, false);
        }
    });

    async function loadElections() {
        const tbody = document.getElementById("electionsTableBody");
        try {
            const res = await Api.get("/elections");
            const elections = res.data;

            if (elections.length === 0) {
                tbody.innerHTML = `<tr><td colspan="5" class="text-muted">No elections yet. Click "New Election" to create one.</td></tr>`;
                return;
            }

            tbody.innerHTML = elections.map((e) => `
                <tr>
                    <td>${UI.escapeHtml(e.title)}</td>
                    <td>${statusBadge(e.status)}</td>
                    <td class="text-muted">${UI.formatDate(e.startTime)} &rarr; ${UI.formatDate(e.endTime)}</td>
                    <td class="mono text-muted">${e.contractAddress ? UI.shortAddress(e.contractAddress) : "Not linked"}</td>
                    <td>
                        <button class="btn btn-secondary btn-sm edit-btn" data-id="${e.electionId}">Edit</button>
                    </td>
                </tr>
            `).join("");

            tbody.querySelectorAll(".edit-btn").forEach((btn) => {
                btn.addEventListener("click", () => {
                    const election = elections.find((e) => e.electionId === parseInt(btn.dataset.id, 10));
                    openModal(election);
                });
            });
        } catch (err) {
            tbody.innerHTML = `<tr><td colspan="5" style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</td></tr>`;
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
})();
