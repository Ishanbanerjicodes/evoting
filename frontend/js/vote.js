// ============================================================================
//  vote.js
//  Logic for vote.html: loads election + candidates, gates on wallet
//  connection, submits the on-chain vote via MetaMask/Ethers, then records
//  the resulting transaction with the backend for audit purposes.
// ============================================================================

(async function init() {
    const user = Api.getUser();
    if (!user || !Api.getToken()) {
        window.location.href = "login.html";
        return;
    }

    const params = new URLSearchParams(window.location.search);
    const electionId = params.get("electionId");
    if (!electionId) {
        window.location.href = "voter-dashboard.html";
        return;
    }

    let selectedCandidateId = null;
    let selectedOnChainIndex = null;
    let election = null;

    await loadElection();

    async function loadElection() {
        try {
            const res = await Api.get(`/elections/${electionId}`);
            election = res.data;

            document.getElementById("electionTitle").textContent = election.title;
            document.getElementById("electionDescription").textContent = election.description || "";
            document.getElementById("statusBadgeTop").textContent = election.status.toUpperCase();

            if (election.status !== "active") {
                document.getElementById("candidateSection").innerHTML = `
                    <div class="empty-state">
                        <div class="empty-icon"><svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 8v4M12 16h.01"/></svg></div>
                        <h4>Voting is not currently open</h4>
                        <p>This election is ${UI.escapeHtml(election.status)}. Check back during the active voting window.</p>
                    </div>`;
                showContent();
                return;
            }

            renderCandidates(election.candidates);

            // Check if already voted (server-side check, source of truth for UI gating)
            const voteCheck = await Api.get(`/vote/check?electionId=${electionId}`);
            if (voteCheck.data.hasVoted) {
                document.getElementById("alreadyVotedBanner").style.display = "block";
                document.getElementById("votedTxHash").textContent = voteCheck.data.txHash;
                document.getElementById("candidateSection").style.display = "none";
                showContent();
                return;
            }

            // Gate on wallet connection.
            if (!Wallet.isConnected()) {
                document.getElementById("walletGate").style.display = "block";
            }

            showContent();
        } catch (err) {
            UI.toast(err.message, "error");
            document.getElementById("loadingState").innerHTML =
                `<p style="color:var(--color-danger)">${UI.escapeHtml(err.message)}</p>`;
        }
    }

    function showContent() {
        document.getElementById("loadingState").style.display = "none";
        document.getElementById("voteContent").style.display = "block";
    }

    function renderCandidates(candidates) {
        const grid = document.getElementById("candidatesGrid");
        if (!candidates || candidates.length === 0) {
            grid.innerHTML = `<p class="text-muted">No candidates have been added to this election yet.</p>`;
            return;
        }

        grid.innerHTML = candidates.map((c) => `
            <div class="card candidate-card" data-candidate-id="${c.candidateId}" data-chain-index="${c.onChainIndex}">
                <div class="candidate-avatar">${UI.initials(c.fullName)}</div>
                <h4 style="margin-bottom:0;">${UI.escapeHtml(c.fullName)}</h4>
                <span class="party-badge">${UI.escapeHtml(c.partyName || "Independent")}</span>
            </div>
        `).join("");

        grid.querySelectorAll(".candidate-card").forEach((card) => {
            card.addEventListener("click", () => {
                grid.querySelectorAll(".candidate-card").forEach((c) => c.classList.remove("selected"));
                card.classList.add("selected");
                selectedCandidateId = parseInt(card.dataset.candidateId, 10);
                selectedOnChainIndex = parseInt(card.dataset.chainIndex, 10);
                document.getElementById("submitVoteBtn").disabled = !Wallet.isConnected();
            });
        });
    }

    // ---- Wallet connect --------------------------------------------------
    document.getElementById("connectWalletBtn").addEventListener("click", async () => {
        const btn = document.getElementById("connectWalletBtn");
        UI.setLoading(btn, true, "Connecting…");
        try {
            const address = await Wallet.connect();
            await Api.post("/link-wallet", { walletAddress: address });
            document.getElementById("walletGate").style.display = "none";
            UI.toast("Wallet connected: " + UI.shortAddress(address), "success");
            if (selectedCandidateId) document.getElementById("submitVoteBtn").disabled = false;
        } catch (err) {
            UI.toast(err.message, "error");
        } finally {
            UI.setLoading(btn, false);
        }
    });

    // ---- Submit vote flow --------------------------------------------------
    document.getElementById("submitVoteBtn").addEventListener("click", () => {
        if (selectedCandidateId === null) {
            UI.toast("Please select a candidate first", "error");
            return;
        }
        const card = document.querySelector(`[data-candidate-id="${selectedCandidateId}"] h4`);
        document.getElementById("confirmCandidateName").textContent = card ? card.textContent : "this candidate";
        document.getElementById("confirmVoteModal").showModal();
    });

    document.getElementById("cancelVoteBtn").addEventListener("click", () => {
        document.getElementById("confirmVoteModal").close();
    });

    document.getElementById("confirmVoteBtn").addEventListener("click", async () => {
        const btn = document.getElementById("confirmVoteBtn");
        UI.setLoading(btn, true, "Confirm in MetaMask…");

        try {
            if (!Wallet.isConnected()) {
                throw new Error("Wallet disconnected. Please reconnect and try again.");
            }

            // 1) Submit the vote transaction on-chain — this is the
            //    authoritative, tamper-evident record.
            const { txHash, blockNumber } = await Wallet.castVote(selectedOnChainIndex);

            UI.toast("Vote confirmed on-chain! Recording with server…", "info");

            // 2) Record the result with the backend for the audit trail
            //    and fast MySQL-backed reads (dashboards, results page).
            await Api.post("/vote", {
                electionId: parseInt(electionId, 10),
                candidateId: selectedCandidateId,
                voterWallet: Wallet.getAddress(),
                txHash,
                blockNumber
            });

            document.getElementById("confirmVoteModal").close();
            UI.toast("Your vote has been recorded successfully!", "success");
            setTimeout(() => { window.location.href = `results.html?electionId=${electionId}`; }, 1500);
        } catch (err) {
            UI.toast(err.message, "error");
        } finally {
            UI.setLoading(btn, false);
        }
    });
})();
