// ============================================================================
//  auth.js
//  Client-side logic for login.html and register.html.
// ============================================================================

function showFieldError(inputEl, show) {
    const group = inputEl.closest(".form-group");
    if (!group) return;
    group.classList.toggle("has-error", show);
}

function redirectAfterLogin(user) {
    if (user.role === "admin") {
        window.location.href = "admin-dashboard.html";
    } else {
        window.location.href = "voter-dashboard.html";
    }
}

// ---- Login page --------------------------------------------------------
const loginForm = document.getElementById("loginForm");
if (loginForm) {
    const togglePasswordBtn = document.getElementById("togglePassword");
    const passwordInput = document.getElementById("password");
    togglePasswordBtn.addEventListener("click", () => {
        passwordInput.type = passwordInput.type === "password" ? "text" : "password";
    });

    loginForm.addEventListener("submit", async (e) => {
        e.preventDefault();
        const emailEl = document.getElementById("email");
        const passwordEl = document.getElementById("password");
        const btn = document.getElementById("loginBtn");

        const emailValid = /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(emailEl.value.trim());
        showFieldError(emailEl, !emailValid);
        showFieldError(passwordEl, passwordEl.value.length === 0);
        if (!emailValid || passwordEl.value.length === 0) return;

        UI.setLoading(btn, true, "Logging in…");
        try {
            const res = await Api.post("/login", {
                email: emailEl.value.trim(),
                password: passwordEl.value
            });
            Api.setToken(res.data.token);
            Api.setUser(res.data.user);
            UI.toast("Welcome back, " + res.data.user.fullName + "!", "success");
            setTimeout(() => redirectAfterLogin(res.data.user), 600);
        } catch (err) {
            UI.toast(err.message, "error");
        } finally {
            UI.setLoading(btn, false);
        }
    });
}

// ---- Register page --------------------------------------------------------
const registerForm = document.getElementById("registerForm");
if (registerForm) {
    let pendingUserId = null;

    registerForm.addEventListener("submit", async (e) => {
        e.preventDefault();

        const fullNameEl = document.getElementById("fullName");
        const voterIdEl = document.getElementById("voterIdNumber");
        const emailEl = document.getElementById("email");
        const passwordEl = document.getElementById("password");
        const confirmEl = document.getElementById("confirmPassword");
        const btn = document.getElementById("registerBtn");

        const emailValid = /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(emailEl.value.trim());
        const strongPassword = /^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)(?=.*[^\w\s]).{8,}$/.test(passwordEl.value);
        const passwordsMatch = passwordEl.value === confirmEl.value;

        showFieldError(fullNameEl, fullNameEl.value.trim().length === 0);
        showFieldError(voterIdEl, voterIdEl.value.trim().length === 0);
        showFieldError(emailEl, !emailValid);
        showFieldError(passwordEl, !strongPassword);
        showFieldError(confirmEl, !passwordsMatch);

        if (!fullNameEl.value.trim() || !voterIdEl.value.trim() || !emailValid || !strongPassword || !passwordsMatch) {
            return;
        }

        UI.setLoading(btn, true, "Creating account…");
        try {
            const res = await Api.post("/register", {
                fullName: fullNameEl.value.trim(),
                voterIdNumber: voterIdEl.value.trim(),
                email: emailEl.value.trim(),
                password: passwordEl.value,
                confirmPassword: confirmEl.value
            });

            pendingUserId = res.data.userId;

            // Instead of popping open an in-page modal (the backdrop-filter +
            // opacity transition it used was the source of the black/blank
            // screen), stash what the OTP page needs and do a plain redirect.
            sessionStorage.setItem("evoting_pending_user_id", pendingUserId);
            if (res.data.otpDevFallback) {
                sessionStorage.setItem("evoting_pending_otp_dev_fallback", res.data.otpDevFallback);
            } else {
                sessionStorage.removeItem("evoting_pending_otp_dev_fallback");
            }

            UI.toast("Account created. Please verify your email.", "success");
            window.location.href = "verify-otp.html";
        } catch (err) {
            UI.toast(err.message, "error");
        } finally {
            UI.setLoading(btn, false);
        }
    });
}
