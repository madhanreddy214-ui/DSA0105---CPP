
#include "httplib.h"
#include "json.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <iomanip>

using namespace std;
using json = nlohmann::json;

struct Pharmacy {
    string id, name, location;
};

struct Medicine {
    string id, pharmacyID, name, batch, expiry;
    int quantity, minStock, demand;
};

vector<Pharmacy> pharmacies;
vector<Medicine> medicines;
vector<json> redistributionHistory;

const string PHARMACY_FILE = "data/pharmacies.csv";
const string MEDICINE_FILE = "data/medicines.csv";

int daysUntil(string date) {
    int y, m, d;

    if (sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3)
        return -99999;

    tm target = {};
    target.tm_year = y - 1900;
    target.tm_mon = m - 1;
    target.tm_mday = d;
    target.tm_isdst = -1;

    time_t targetTime = mktime(&target);

    if (targetTime == -1)
        return -99999;

    // Reject impossible calendar dates.
    tm checked = *localtime(&targetTime);

    if (checked.tm_year != y - 1900 ||
        checked.tm_mon != m - 1 ||
        checked.tm_mday != d)
        return -99999;

    time_t now = time(nullptr);
    tm today = *localtime(&now);

    today.tm_hour = 0;
    today.tm_min = 0;
    today.tm_sec = 0;
    today.tm_isdst = -1;

    time_t todayTime = mktime(&today);

    return static_cast<int>(
        difftime(targetTime, todayTime) / 86400
    );
}

string pharmacyName(string id) {
    for (const auto& p : pharmacies) {
        if (p.id == id)
            return p.name;
    }
    return "Unknown";
}

json pharmacyJSON(const Pharmacy& p) {
    return {
        {"id", p.id},
        {"name", p.name},
        {"location", p.location}
    };
}

json medicineJSON(const Medicine& m) {
    int days = daysUntil(m.expiry);
    string status = "Healthy";

    if (days == -99999)
        status = "Invalid expiry date";
    else if (days < 0)
        status = "Expired";
    else if (days <= 30)
        status = "Critical";
    else if (days <= 90)
        status = "Expiring Soon";

    int required = max(m.minStock, m.demand);

    return {
        {"id", m.id},
        {"pharmacyID", m.pharmacyID},
        {"pharmacyName", pharmacyName(m.pharmacyID)},
        {"name", m.name},
        {"batch", m.batch},
        {"expiry", m.expiry},
        {"quantity", m.quantity},
        {"minStock", m.minStock},
        {"demand", m.demand},
        {"daysToExpiry", days},
        {"status", status},
        {"shortage", max(0, required - m.quantity)}
    };
}

void saveData() {
    ofstream pf(PHARMACY_FILE);

    pf << "id,name,location\n";

    for (const auto& p : pharmacies)
        pf << p.id << "," << p.name << "," << p.location << "\n";

    pf.close();

    ofstream mf(MEDICINE_FILE);

    mf << "id,pharmacyID,name,batch,expiry,quantity,minStock,demand\n";

    for (const auto& m : medicines) {
        mf << m.id << "," << m.pharmacyID << ","
           << m.name << "," << m.batch << ","
           << m.expiry << "," << m.quantity << ","
           << m.minStock << "," << m.demand << "\n";
    }

    mf.close();
}

void loadData() {
    ifstream pf(PHARMACY_FILE);
    string line;

    getline(pf, line); // Skip header.

    while (getline(pf, line)) {
        stringstream ss(line);
        Pharmacy p;

        getline(ss, p.id, ',');
        getline(ss, p.name, ',');
        getline(ss, p.location);

        if (!p.id.empty())
            pharmacies.push_back(p);
    }

    pf.close();

    ifstream mf(MEDICINE_FILE);

    getline(mf, line); // Skip header.

    while (getline(mf, line)) {
        stringstream ss(line);
        Medicine m;
        string q, min, demand;

        getline(ss, m.id, ',');
        getline(ss, m.pharmacyID, ',');
        getline(ss, m.name, ',');
        getline(ss, m.batch, ',');
        getline(ss, m.expiry, ',');
        getline(ss, q, ',');
        getline(ss, min, ',');
        getline(ss, demand);

        try {
            m.quantity = stoi(q);
            m.minStock = stoi(min);
            m.demand = stoi(demand);

            if (!m.id.empty())
                medicines.push_back(m);
        }
        catch (...) {
            cerr << "Skipping invalid medicine record.\n";
        }
    }

    mf.close();
}

bool pharmacyExists(string id) {
    for (const auto& p : pharmacies)
        if (p.id == id) return true;

    return false;
}

void initializeFiles() {
    ofstream pf(PHARMACY_FILE, ios::app);
    pf.close();

    ofstream mf(MEDICINE_FILE, ios::app);
    mf.close();

    ifstream checkP(PHARMACY_FILE);
    if (checkP.peek() == ifstream::traits_type::eof()) {
        checkP.close();
        ofstream out(PHARMACY_FILE);
        out << "id,name,location\n";
    }

    ifstream checkM(MEDICINE_FILE);
    if (checkM.peek() == ifstream::traits_type::eof()) {
        checkM.close();
        ofstream out(MEDICINE_FILE);
        out << "id,pharmacyID,name,batch,expiry,quantity,minStock,demand\n";
    }
}

int main() {
    initializeFiles();
    loadData();

    httplib::Server server;

    // Allow requests from the local website.
    server.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Headers", "Content-Type"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}
    });

    server.Options(R"(/.*)", [](const httplib::Request&,
                                httplib::Response& res) {
        res.status = 204;
    });

    // Health check.
    server.Get("/api/health", [](const httplib::Request&,
                                 httplib::Response& res) {
        res.set_content(
            R"({"status":"online","backend":"C++17"})",
            "application/json"
        );
    });

    // Dashboard statistics.
    server.Get("/api/dashboard", [](const httplib::Request&,
                                    httplib::Response& res) {
        int expiring = 0;
        int lowStock = 0;
        int expired = 0;

        for (const auto& m : medicines) {
            int days = daysUntil(m.expiry);

            if (days >= 0 && days <= 90)
                expiring++;

            if (days < 0 && days != -99999)
                expired++;

            if (m.quantity < max(m.minStock, m.demand))
                lowStock++;
        }

        json result = {
            {"medicineRecords", medicines.size()},
            {"expiringSoon", expiring},
            {"pharmacyPartners", pharmacies.size()},
            {"lowStock", lowStock},
            {"expired", expired}
        };

        res.set_content(result.dump(), "application/json");
    });

    // Get pharmacies.
    server.Get("/api/pharmacies", [](const httplib::Request&,
                                     httplib::Response& res) {
        json result = json::array();

        for (const auto& p : pharmacies)
            result.push_back(pharmacyJSON(p));

        res.set_content(result.dump(), "application/json");
    });

    // Register pharmacy.
    server.Post("/api/pharmacies", [](const httplib::Request& req,
                                      httplib::Response& res) {
        try {
            json data = json::parse(req.body);

            string name = data.value("name", "");
            string location = data.value("location", "");

            if (name.empty() || name.find_first_of(",\r\n") != string::npos ||
                location.find_first_of(",\r\n") != string::npos) {
                res.status = 400;
                res.set_content(
                    R"({"error":"Enter valid pharmacy details."})",
                    "application/json"
                );
                return;
            }

            Pharmacy p;
            p.id = "P" + to_string(1001 + pharmacies.size());
            p.name = name;
            p.location = location;

            pharmacies.push_back(p);
            saveData();

            res.status = 201;
            res.set_content(pharmacyJSON(p).dump(), "application/json");
        }
        catch (...) {
            res.status = 400;
            res.set_content(
                R"({"error":"Invalid request."})",
                "application/json"
            );
        }
    });

    // Get all medicines.
    server.Get("/api/medicines", [](const httplib::Request&,
                                    httplib::Response& res) {
        json result = json::array();

        for (const auto& m : medicines)
            result.push_back(medicineJSON(m));

        res.set_content(result.dump(), "application/json");
    });

    // Add medicine.
    server.Post("/api/medicines", [](const httplib::Request& req,
                                     httplib::Response& res) {
        try {
            json data = json::parse(req.body);

            Medicine m;

            m.pharmacyID = data.value("pharmacyID", "");
            m.name = data.value("name", "");
            m.batch = data.value("batch", "");
            m.expiry = data.value("expiry", "");
            m.quantity = data.value("quantity", -1);
            m.minStock = data.value("minStock", -1);
            m.demand = data.value("demand", -1);

            if (!pharmacyExists(m.pharmacyID) ||
                m.name.empty() || m.batch.empty() ||
                m.name.find_first_of(",\r\n") != string::npos ||
                m.batch.find_first_of(",\r\n") != string::npos ||
                m.quantity < 0 || m.minStock < 0 || m.demand < 0 ||
                daysUntil(m.expiry) == -99999) {

                res.status = 400;
                res.set_content(
                    R"({"error":"Check pharmacy, medicine details, expiry date and quantities."})",
                    "application/json"
                );
                return;
            }

            m.id = "M" + to_string(1001 + medicines.size());

            medicines.push_back(m);
            saveData();

            res.status = 201;
            res.set_content(medicineJSON(m).dump(), "application/json");
        }
        catch (...) {
            res.status = 400;
            res.set_content(
                R"({"error":"Invalid medicine data."})",
                "application/json"
            );
        }
    });

    // Expiry and shortage report.
    server.Get("/api/alerts", [](const httplib::Request&,
                                 httplib::Response& res) {
        json expiry = json::array();
        json shortage = json::array();

        for (const auto& m : medicines) {
            int days = daysUntil(m.expiry);
            json item = medicineJSON(m);

            if (days <= 90)
                expiry.push_back(item);

            if (m.quantity < max(m.minStock, m.demand))
                shortage.push_back(item);
        }

        res.set_content(
            json({
                {"expiryAlerts", expiry},
                {"shortageAlerts", shortage}
            }).dump(),
            "application/json"
        );
    });

    // Intelligent redistribution.
    server.Post("/api/redistribute", [](const httplib::Request&,
                                        httplib::Response& res) {
        json transfers = json::array();

        for (auto& receiver : medicines) {
            int required = max(receiver.minStock, receiver.demand);
            int shortage = required - receiver.quantity;

            if (shortage <= 0 || daysUntil(receiver.expiry) <= 0)
                continue;

            for (auto& donor : medicines) {
                if (donor.pharmacyID == receiver.pharmacyID)
                    continue;

                if (donor.name != receiver.name)
                    continue;

                // Match the same medicine batch and strength.
                if (daysUntil(donor.expiry) <= 0)
                    continue;

                int donorRequired = max(donor.minStock, donor.demand);
                int surplus = donor.quantity - donorRequired;

                if (surplus <= 0)
                    continue;

                int amount = min(surplus, shortage);

                donor.quantity -= amount;
                receiver.quantity += amount;
                shortage -= amount;

                json transfer = {
                    {"medicine", receiver.name},
                    {"from", pharmacyName(donor.pharmacyID)},
                    {"to", pharmacyName(receiver.pharmacyID)},
                    {"quantity", amount},
                    {"batch", donor.batch},
                    {"expiry", donor.expiry}
                };

                transfers.push_back(transfer);
                redistributionHistory.push_back(transfer);

                if (shortage == 0)
                    break;
            }
        }

        saveData();

        res.set_content(
            json({{"transfers", transfers}}).dump(),
            "application/json"
        );
    });

    // Redistribution history for this server session.
    server.Get("/api/redistributions", [](const httplib::Request&,
                                          httplib::Response& res) {
        res.set_content(
            nlohmann::json(redistributionHistory).dump(),
            "application/json"
        );
    });

    // Serve website files.
    if (!server.set_mount_point("/", "./frontend")) {
        cerr << "Could not find frontend folder. Run from project root.\n";
        return 1;
    }

    cout << "\n=========================================\n";
    cout << " MEDICINE EXPIRY PREVENTION SYSTEM\n";
    cout << " C++ BACKEND SERVER\n";
    cout << "=========================================\n";
    cout << "Server running at http://localhost:8080\n";
    cout << "Press CTRL+C to stop.\n";

    if (!server.listen("127.0.0.1", 8080)) {
        cerr << "Could not start server. Port 8080 may be in use.\n";
        return 1;
    }

    return 0;
}
