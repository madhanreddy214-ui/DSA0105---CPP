
const API = "/api";

const $ = id => document.getElementById(id);

function escapeHTML(value) {
    return String(value ?? "").replace(/[&<>"']/g, char => ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#39;"
    })[char]);
}

async function api(path, options = {}) {
    const response = await fetch(API + path, {
        ...options,
        headers: {
            "Content-Type": "application/json",
            ...(options.headers || {})
        }
    });

    const data = await response.json();

    if (!response.ok)
        throw new Error(data.error || "Request failed");

    return data;
}

function toast(message) {
    const element = $("toast");
    element.textContent = message;
    element.classList.add("show");

    setTimeout(() => element.classList.remove("show"), 3000);
}

function showPage(page) {
    document.querySelectorAll(".page").forEach(section => {
        section.classList.toggle("active", section.id === page);
    });

    document.querySelectorAll(".nav").forEach(button => {
        button.classList.toggle("active", button.dataset.page === page);
    });

    $("pageName").textContent = page.toUpperCase();
}

document.querySelectorAll(".nav").forEach(button => {
    button.addEventListener("click", () => showPage(button.dataset.page));
});

async function loadDashboard() {
    const data = await api("/dashboard");

    $("totalMedicines").textContent = data.medicineRecords;
    $("expiringCount").textContent = data.expiringSoon;
    $("totalPharmacies").textContent = data.pharmacyPartners;
    $("shortageCount").textContent = data.lowStock;
}

async function loadPharmacies() {
    const pharmacies = await api("/pharmacies");

    $("pharmacyList").innerHTML = pharmacies.length
        ? pharmacies.map(p => `
            <div class="pharmacy-card">
                <span class="pharmacy-icon">+</span>
                <div>
                    <h3>${escapeHTML(p.name)}</h3>
                    <p>${escapeHTML(p.id)}</p>
                    <small>${escapeHTML(p.location)}</small>
                </div>
            </div>
        `).join("")
        : "<p>No pharmacies registered yet.</p>";

    $("medicinePharmacy").innerHTML =
        '<option value="">Select pharmacy</option>' +
        pharmacies.map(p =>
            `<option value="${escapeHTML(p.id)}">${escapeHTML(p.name)} (${escapeHTML(p.id)})</option>`
        ).join("");
}

async function loadMedicines() {
    const medicines = await api("/medicines");

    $("medicineTable").innerHTML = medicines.length
        ? medicines.map(m => `
            <tr>
                <td><strong>${escapeHTML(m.name)}</strong></td>
                <td>${escapeHTML(m.pharmacyName)}</td>
                <td>${m.quantity}</td>
                <td>${m.demand}</td>
                <td>${escapeHTML(m.expiry)}</td>
                <td><span class="status ${statusClass(m.status)}">${escapeHTML(m.status)}</span></td>
            </tr>
        `).join("")
        : '<tr><td colspan="6">No medicine records found.</td></tr>';

    $("expiryTable").innerHTML = medicines
        .filter(m => m.daysToExpiry <= 90)
        .sort((a, b) => a.daysToExpiry - b.daysToExpiry)
        .map(m => `
            <tr>
                <td>${escapeHTML(m.name)}</td>
                <td>${escapeHTML(m.batch)}</td>
                <td>${escapeHTML(m.pharmacyName)}</td>
                <td>${m.quantity}</td>
                <td>${escapeHTML(m.expiry)}</td>
                <td><span class="status ${statusClass(m.status)}">${escapeHTML(m.status)}</span></td>
            </tr>
        `).join("") || '<tr><td colspan="6">No expiry alerts.</td></tr>';

    $("shortageList").innerHTML = medicines
        .filter(m => m.shortage > 0)
        .map(m => `
            <div class="alert-item">
                <div>
                    <strong>${escapeHTML(m.name)}</strong>
                    <p>${escapeHTML(m.pharmacyName)}</p>
                </div>
                <span class="status critical">Shortage: ${m.shortage}</span>
            </div>
        `).join("") || "<p>No medicine shortages detected.</p>";
}

function statusClass(status) {
    if (status === "Expired" || status === "Critical")
        return "critical";

    if (status === "Expiring Soon")
        return "warning";

    if (status === "Invalid expiry date")
        return "critical";

    return "healthy";
}

$("pharmacyForm").addEventListener("submit", async event => {
    event.preventDefault();

    try {
        const result = await api("/pharmacies", {
            method: "POST",
            body: JSON.stringify({
                name: $("pharmacyName").value.trim(),
                location: $("pharmacyLocation").value.trim()
            })
        });

        toast("Pharmacy registered: " + result.id);
        event.target.reset();

        await loadAll();
    } catch (error) {
        toast(error.message);
    }
});

$("medicineForm").addEventListener("submit", async event => {
    event.preventDefault();

    try {
        await api("/medicines", {
            method: "POST",
            body: JSON.stringify({
                pharmacyID: $("medicinePharmacy").value,
                name: $("medicineName").value.trim(),
                batch: $("medicineBatch").value.trim(),
                expiry: $("medicineExpiry").value,
                quantity: Number($("medicineQuantity").value),
                minStock: Number($("medicineMin").value),
                demand: Number($("medicineDemand").value)
            })
        });

        toast("Medicine added successfully!");
        event.target.reset();

        await loadAll();
    } catch (error) {
        toast(error.message);
    }
});

async function runRedistribution() {
    const button = $("redistributeButton");
    button.disabled = true;
    button.textContent = "Analyzing stock...";

    try {
        const result = await api("/redistribute", {
            method: "POST",
            body: "{}"
        });

        const transfers = result.transfers;

        $("redistributionResults").innerHTML = transfers.length
            ? transfers.map(t => `
                <div class="alert-item transfer">
                    <div>
                        <strong>${escapeHTML(t.medicine)}</strong>
                        <p>${escapeHTML(t.from)} → ${escapeHTML(t.to)}</p>
                        <small>Batch ${escapeHTML(t.batch)} | Expires ${escapeHTML(t.expiry)}</small>
                    </div>
                    <span class="status healthy">${t.quantity} units</span>
                </div>
            `).join("")
            : "<p>No suitable transfers found. Check surplus stock and shortages.</p>";

        toast(transfers.length + " redistribution recommendation(s) generated.");

        await loadAll();
        await loadHistory();
    } catch (error) {
        toast(error.message);
    } finally {
        button.disabled = false;
        button.textContent = "Run redistribution analysis";
    }
}

async function loadHistory() {
    const history = await api("/redistributions");

    $("transferHistory").innerHTML = history.length
        ? history.map(t => `
            <div class="alert-item">
                <div>
                    <strong>${escapeHTML(t.medicine)}</strong>
                    <p>${escapeHTML(t.from)} → ${escapeHTML(t.to)}</p>
                </div>
                <span class="status healthy">${t.quantity} units</span>
            </div>
        `).join("")
        : "<p>No transfers recorded in this session.</p>";
}

$("redistributeButton").addEventListener("click", runRedistribution);

async function loadAll() {
    try {
        await loadDashboard();
        await loadPharmacies();
        await loadMedicines();

        $("connectionStatus").textContent = "● SERVER CONNECTED";
        $("connectionStatus").classList.add("online");
    } catch (error) {
        $("connectionStatus").textContent = "● SERVER DISCONNECTED";
        $("connectionStatus").classList.remove("online");
        toast(error.message);
    }
}

async function initialize() {
    $("today").textContent = new Date().toLocaleDateString("en-IN", {
        day: "numeric",
        month: "short",
        year: "numeric"
    });

    try {
        await api("/health");
        await loadAll();
        await loadHistory();
    } catch (error) {
        $("connectionStatus").textContent = "● SERVER DISCONNECTED";
        console.error(error);
        toast("Cannot connect to C++ backend. Start the server first.");
    }
}

initialize();
