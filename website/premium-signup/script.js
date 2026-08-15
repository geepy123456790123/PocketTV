const checkoutButton = document.getElementById("checkoutButton");
const toast = document.getElementById("toast");

const CHECKOUT_ENDPOINT = "/api/create-checkout-session";

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
