#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/* ================= CONFIG ================= */
#define LISTEN_PORT 44769
#define MYSQL_HOST "localhost"
#define MYSQL_USER "wordpress"
#define MYSQL_PASS "wY*XUv^LqbH51tWjfI3K"
#define MYSQL_DB   "wordpress"
/* ========================================== */

std::vector<json> itemlines;

/* -------- Extract JSON from HTTP -------- */
std::string extract_json_from_http(const std::string& raw) {
    size_t pos = raw.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return raw.substr(pos + 4);
}

/* -------- Query MySQL by Email -------- */
std::vector<json> query_mysql_by_email(const std::string& email) {
    std::vector<json> results;

    MYSQL* conn = mysql_init(nullptr);
    if (!mysql_real_connect(conn,
        MYSQL_HOST,
        MYSQL_USER,
        MYSQL_PASS,
        MYSQL_DB,
        0, nullptr, 0)) {
        std::cerr << "❌ MySQL connect failed: "
                  << mysql_error(conn) << "\n";
        return results;
    }

    std::string query =
        "SELECT * FROM wp_wc_orders WHERE billing_email = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    mysql_stmt_prepare(stmt, query.c_str(), query.length());

    MYSQL_BIND bind_param{};
    bind_param.buffer_type = MYSQL_TYPE_STRING;
    bind_param.buffer = (void*)email.c_str();
    bind_param.buffer_length = email.size();

    mysql_stmt_bind_param(stmt, &bind_param);
    mysql_stmt_execute(stmt);

    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    int cols = mysql_num_fields(meta);

    std::vector<char> row_buffer(4096);
    std::vector<MYSQL_BIND> bind_result(cols);

    for (int i = 0; i < cols; i++) {
        bind_result[i].buffer_type = MYSQL_TYPE_STRING;
        bind_result[i].buffer = row_buffer.data();
        bind_result[i].buffer_length = row_buffer.size();
    }

    mysql_stmt_bind_result(stmt, bind_result.data());

    while (mysql_stmt_fetch(stmt) == 0) {
        json row;
        MYSQL_FIELD* fields = mysql_fetch_fields(meta);

        for (int i = 0; i < cols; i++) {
            row[fields[i].name] = std::string((char*)bind_result[i].buffer);
        }
        results.push_back(row);
    }

    mysql_free_result(meta);
    mysql_stmt_close(stmt);
    mysql_close(conn);

    return results;
}

/* -------- Handle Stripe payment_intent -------- */
void handle_payment_intent_succeeded(const json& intent) {
    std::string email;

    if (intent.contains("receipt_email") && !intent["receipt_email"].is_null()) {
        email = intent["receipt_email"];
    } else {
        email = intent["charges"]["data"][0]["billing_details"]["email"];
    }

    std::cout << "✅ payment_intent.succeeded for email: "
              << email << "\n";

    auto rows = query_mysql_by_email(email);

    for (auto& r : rows)
        itemlines.push_back(r);

    std::cout << "✅ itemlines from MySQL:\n"
              << json(itemlines).dump(2) << "\n";

    itemlines.clear();
    std::cout << "✅ itemlines reset.\n";
}

/* -------- Socket Listener -------- */
int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(LISTEN_PORT);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    std::cout << "Listening on port " << LISTEN_PORT << "...\n";

    int client = accept(server_fd, nullptr, nullptr);

    char buffer[8192];
    int bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
    buffer[bytes] = 0;

    std::string raw_http(buffer);
    std::cout << "Raw HTTP received:\n" << raw_http << "\n";

    std::string json_body = extract_json_from_http(raw_http);
    if (json_body.empty()) {
        std::cerr << "❌ Could not parse JSON body\n";
        return 1;
    }

    json payload;
    try {
        payload = json::parse(json_body);
    } catch (...) {
        std::cerr << "❌ JSON decode failed\n";
        return 1;
    }

    std::string event_type = payload["type"];
    if (event_type == "payment_intent.succeeded") {
        handle_payment_intent_succeeded(payload["data"]["object"]);
    } else {
        std::cout << "Ignored event: " << event_type << "\n";
    }

    close(client);
    close(server_fd);
    return 0;
}
