// ============================================================================
//  ui.js
//  Shared UI utilities used across every page: toast notifications,
//  dark/light theme toggling (persisted in localStorage), confirmation
//  dialogs, and small DOM helpers.
// ============================================================================

const UI = (() => {
    // ---- Theme -------------------------------------------------------
    function applyStoredTheme() {
        const saved = localStorage.getItem("evoting_theme");
        if (saved) {
            document.documentElement.setAttribute("data-theme", saved);
        }
    }

    function toggleTheme() {
        const current = document.documentElement.getAttribute("data-theme") ||
            (window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
        const next = current === "dark" ? "light" : "dark";
        document.documentElement.setAttribute("data-theme", next);
        localStorage.setItem("evoting_theme", next);
        updateThemeToggleIcon(next);
    }

    function updateThemeToggleIcon(theme) {
        document.querySelectorAll("[data-theme-toggle]").forEach((btn) => {
            btn.innerHTML = theme === "dark" ? sunIcon() : moonIcon();
        });
    }

    function sunIcon() {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>`;
    }
    function moonIcon() {
        return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>`;
    }

    // ---- Toasts -------------------------------------------------------
    function ensureToastStack() {
        let stack = document.querySelector(".toast-stack");
        if (!stack) {
            stack = document.createElement("div");
            stack.className = "toast-stack";
            document.body.appendChild(stack);
        }
        return stack;
    }

    function toast(message, type = "info", duration = 4200) {
        const stack = ensureToastStack();
        const el = document.createElement("div");
        el.className = `toast toast-${type}`;

        const icons = {
            success: `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#1f9d55" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M8 12l3 3 5-6"/></svg>`,
            error: `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#dc2626" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 8v5M12 16h.01"/></svg>`,
            info: `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#0ea5a4" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/></svg>`
        };

        el.innerHTML = `<span class="toast-icon">${icons[type] || icons.info}</span><span>${escapeHtml(message)}</span>`;
        stack.appendChild(el);

        setTimeout(() => {
            el.classList.add("toast-out");
            setTimeout(() => el.remove(), 300);
        }, duration);
    }

    // ---- Confirm dialog -------------------------------------------------
    function confirmDialog(message, title = "Are you sure?") {
        return new Promise((resolve) => {
            const dialog = document.createElement("dialog");
            dialog.className = "simple-modal";
            dialog.innerHTML = `
                <h4>${escapeHtml(title)}</h4>
                <p>${escapeHtml(message)}</p>
                <div style="display:flex; gap:12px; margin-top:24px;">
                    <button class="btn btn-secondary btn-block" data-action="cancel">Cancel</button>
                    <button class="btn btn-primary btn-block" data-action="confirm">Confirm</button>
                </div>`;
            document.body.appendChild(dialog);
            dialog.showModal();

            dialog.addEventListener("click", (e) => {
                const action = e.target.getAttribute("data-action");
                if (action === "confirm") { cleanup(true); }
                // Clicking the ::backdrop itself lands directly on the
                // <dialog> element (not any of its children) — that's how
                // "click outside to cancel" is detected here.
                else if (action === "cancel" || e.target === dialog) { cleanup(false); }
            });

            function cleanup(result) {
                dialog.close();
                dialog.remove();
                resolve(result);
            }
        });
    }

    // ---- Helpers -------------------------------------------------------
    function escapeHtml(str) {
        const div = document.createElement("div");
        div.textContent = str;
        return div.innerHTML;
    }

    function formatDate(isoString) {
        if (!isoString) return "—";
        const d = new Date(isoString.replace(" ", "T"));
        if (isNaN(d.getTime())) return isoString;
        return d.toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" }) +
               " " + d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" });
    }

    function shortAddress(address) {
        if (!address || address.length < 10) return address || "—";
        return address.slice(0, 6) + "…" + address.slice(-4);
    }

    function initials(name) {
        if (!name) return "?";
        return name.trim().split(/\s+/).slice(0, 2).map((p) => p[0].toUpperCase()).join("");
    }

    function setLoading(button, isLoading, loadingText = "Please wait…") {
        if (isLoading) {
            button.dataset.originalText = button.innerHTML;
            button.innerHTML = `<span class="spinner"></span> ${loadingText}`;
            button.disabled = true;
        } else {
            button.innerHTML = button.dataset.originalText || button.innerHTML;
            button.disabled = false;
        }
    }

    // Initialize theme + toggle icon on every page load.
    document.addEventListener("DOMContentLoaded", () => {
        applyStoredTheme();
        const current = document.documentElement.getAttribute("data-theme") ||
            (window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
        updateThemeToggleIcon(current);
        document.querySelectorAll("[data-theme-toggle]").forEach((btn) => {
            btn.addEventListener("click", toggleTheme);
        });
    });

    return { toast, confirmDialog, escapeHtml, formatDate, shortAddress, initials, setLoading, toggleTheme };
})();
