const checkoutButton = document.getElementById("checkoutButton");
const toast = document.getElementById("toast");
const activationCodeCard = document.getElementById("activationCodeCard");
const activationCode = document.getElementById("activationCode");

const CHECKOUT_ENDPOINT = "https://api.pocket-tv.net/api/create-checkout-session";
const CODE_ENDPOINT = "https://api.pocket-tv.net/api/checkout-code";

showCheckoutResult();

checkoutButton?.addEventListener("click", async () => {
  checkoutButton.disabled = true;
  checkoutButton.textContent = "Opening checkout...";

  try {
    const response = await fetch(CHECKOUT_ENDPOINT, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ plan: "pockettv-premium-monthly" })
    });

    if (!response.ok) throw new Error("Checkout endpoint is not configured yet.");

    const payload = await response.json();
    if (!payload.url) throw new Error("Checkout response did not include a URL.");

    window.location.assign(payload.url);
  } catch (error) {
    showToast(
      "Checkout backend is not connected yet. Create a Stripe Checkout endpoint with a 7-day trial and $9.99 monthly price."
    );
    checkoutButton.disabled = false;
    checkoutButton.textContent = "Start free trial";
  }
});

function showToast(message) {
  if (!toast) return;
  toast.textContent = message;
  toast.classList.add("visible");
  window.setTimeout(() => toast.classList.remove("visible"), 5200);
}

async function showCheckoutResult() {
  const params = new URLSearchParams(window.location.search);
  if (params.get("checkout") === "cancelled") {
    showToast("Checkout was cancelled. Your trial has not started.");
    return;
  }

  const sessionId = params.get("session_id");
  if (params.get("checkout") !== "success" || !sessionId) return;

  if (activationCodeCard) activationCodeCard.hidden = false;
  if (activationCode) activationCode.textContent = "Loading...";
  try {
    const response = await fetch(`${CODE_ENDPOINT}?session_id=${encodeURIComponent(sessionId)}`);
    if (!response.ok) throw new Error("Activation code is not ready yet.");
    const payload = await response.json();
    if (activationCode && payload.code) activationCode.textContent = payload.code;
    showToast("Your PocketTV activation code is ready.");
  } catch {
    if (activationCode) activationCode.textContent = "Check email";
    showToast("Your subscription is active. If the code is not visible yet, refresh this page in a moment.");
  }
}
