// ============================================================================
//  verify-otp.js
//  Client-side logic for verify-otp.html. This page replaces the old
//  in-page OTP modal on register.html: register.html now just redirects
//  here with the pending user id stashed in sessionStorage, so there's no
//  backdrop/opacity/blur transition involved at all — just a normal page
//  load, which sidesteps the black/blank-screen rendering issue some
//  browsers had with the modal's backdrop-filter + opacity transition.
// ============================================================================

(function () {
    const otpForm = document.getElementById("otpForm");
    if (!otpForm) return; // safety guard if script gets included elsewhere

    const otpText = document.getElementById("otpText");
    const otpInput = document.getElementById("otpInput");
    const verifyBtn = document.getElementById("verifyOtpBtn");
    const resendBtn = document.getElementById("resendOtpBtn");

    // sessionStorage only stores strings, but the backend's JSON parser
    // expects userId as a JSON *number* (its getInt() silently returns 0
    // for a string value) - so convert it back to a number here, otherwise
    // every verify/resend call silently targets user 0 and fails with
    // "Invalid or expired OTP".
    const pendingUserId = Number(sessionStorage.getItem("evoting_pending_user_id"));
    const otpDevFallback = sessionStorage.getItem("evoting_pending_otp_dev_fallback");

    // No pending registration in this browser tab/session — nothing to
    // verify, so send the person back to register instead of showing a
    // dead form.
    if (!pendingUserId) {
        window.location.href = "register.html";
        return;
    }

    function showDevFallback(code) {
        otpText.innerHTML =
            "The server could not send an email (SMTP isn't configured), so here is your " +
            "verification code directly:<br><span class=\"code-callout mono\">" +
            UI.escapeHtml(code) + "</span>";
        otpInput.value = code;
    }

    if (otpDevFallback) {
        showDevFallback(otpDevFallback);
        sessionStorage.removeItem("evoting_pending_otp_dev_fallback");
    } else {
        otpText.textContent = "A verification code has been sent to your email.";
    }

    otpInput.focus();

    otpForm.addEventListener("submit", async (e) => {
        e.preventDefault();

        if (otpInput.value.trim().length !== 6) {
            showFieldError(otpInput, true);
            UI.toast("Please enter the 6-digit code", "error");
            return;
        }
        showFieldError(otpInput, false);

        UI.setLoading(verifyBtn, true, "Verifying…");
        try {
            await Api.post("/verify-otp", {
                userId: pendingUserId,
                otpCode: otpInput.value.trim()
            });
            sessionStorage.removeItem("evoting_pending_user_id");
            UI.toast("Account verified! Redirecting to login…", "success");
            setTimeout(() => { window.location.href = "login.html"; }, 1000);
        } catch (err) {
            UI.toast(err.message, "error");
        } finally {
            UI.setLoading(verifyBtn, false);
        }
    });

    resendBtn.addEventListener("click", async () => {
        UI.setLoading(resendBtn, true, "Resending…");
        try {
            const res = await Api.post("/resend-otp", { userId: pendingUserId });
            otpInput.value = "";

            if (res.data.otpDevFallback) {
                showDevFallback(res.data.otpDevFallback);
                UI.toast("New code generated. SMTP isn't configured — shown on-screen.", "info");
            } else {
                otpText.textContent = "A new verification code has been sent to your email.";
                UI.toast("A new code has been sent to your email.", "success");
            }
            otpInput.focus();
        } catch (err) {
            UI.toast(err.message, "error");
        } finally {
            UI.setLoading(resendBtn, false);
        }
    });

    function showFieldError(inputEl, show) {
        const group = inputEl.closest(".form-group");
        if (!group) return;
        group.classList.toggle("has-error", show);
    }
})();
