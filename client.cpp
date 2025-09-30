#include <iostream>
#include <string>
#include <winsock2.h>
#include <windows.h>
#include <cmath>
#include <mutex>
#include <random>
#include <thread>
#include <limits>

using namespace std;

void LogEvent(const string& message) {
    printf("[CLIENT] %s\n", message.c_str());
}

int main(int argc, char* argv[]) {
    int clientId;
    if (argc < 2) {
        cout << "Usage: client.exe <ClientID>" << endl;
        cout << "Example: client.exe 1" << endl;
        cout << "Please enter Client ID manually: ";

        // Чтение ID из консоли
        string input;
        getline(cin, input);

        try {
            clientId = stoi(input);
        } catch (const exception& e) {
            cout << "Invalid Client ID! Please enter a number." << endl;
            cout << "Press Enter to exit..." << endl;
            cin.get();
            return 1;
        }

        cout << "Using Client ID: " << clientId << endl;
    } else {
        clientId = atoi(argv[1]);
    }

    LogEvent("Client " + to_string(clientId) + " starting...");

    // Инициализация Winsock
    WSADATA wsData;
    if (WSAStartup(MAKEWORD(2, 2), &wsData) != 0) {
        LogEvent("WSAStartup failed!");

        cout << "Press Enter to exit..." << endl;
        cin.get();
        return 1;
    }

    // Создание сокета
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        LogEvent("Socket creation failed!");
        WSACleanup();

        cout << "Press Enter to exit..." << endl;
        cin.get();
        return 1;
    }

    // Настройка адреса сервера
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(54000);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Подключение к серверу
    LogEvent("Connecting to barber shop at 127.0.0.1:54000...");
    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        LogEvent("Connection failed! Make sure server is running.");
        closesocket(clientSocket);
        WSACleanup();

        cout << "Press Enter to exit..." << endl;
        cin.get();
        return 1;
    }

    LogEvent("Successfully connected to barber shop!");

    // Получаем приветствие от сервера
    char buffer[1024];
    int bytesReceived;

    // Получаем все начальные сообщения от сервера
    while (true) {
        bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            string message = string(buffer);
            LogEvent("Server: " + message);

            // Если получили информацию об очереди, можно отправить приветствие
            if (message.find("queue") != string::npos || message.find("Client") != string::npos) {
                break;
            }
        } else {
            break;
        }
    }

    // Отправляем приветствие сразу после подключения
    string helloMsg = "Client " + to_string(clientId) + ": Hello! I just arrived and waiting for haircut";
    send(clientSocket, helloMsg.c_str(), helloMsg.size(), 0);
    LogEvent("Sent greeting to server");

    // Основной цикл ожидания событий
    bool haircutStarted = false;
    bool haircutFinished = false;

    while (!haircutFinished) {
        bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            string response = string(buffer);

            if (response == "HAIRCUT_STARTED") {
                LogEvent("=== MY TURN! HAIRCUT STARTED! ===");
                haircutStarted = true;

                // Отправляем сообщение о том, что рад началу стрижки
                string excitedMsg = "Client " + to_string(clientId) + ": Finally! I'm ready for haircut!";
                send(clientSocket, excitedMsg.c_str(), excitedMsg.size(), 0);
                LogEvent("Expressed excitement about haircut start");

            } else if (response == "HAIRCUT_FINISHED") {
                LogEvent("=== HAIRCUT FINISHED! THANK YOU! ===");
                haircutFinished = true;

            } else {
                LogEvent("Server: " + response);

                // Если стрижка еще не началась, можно отправлять дополнительные сообщения
                if (!haircutStarted) {
                    // Случайная задержка
                    random_device rd;
                    mt19937 gen(rd());
                    uniform_int_distribution<> dis(2000, 5000);
                    this_thread::sleep_for(chrono::milliseconds(dis(gen)));

                    // Отправляем вопрос о статусе
                    string statusMsg = "Client " + to_string(clientId) + ": How much longer should I wait?";
                    send(clientSocket, statusMsg.c_str(), statusMsg.size(), 0);
                    LogEvent("Asked about waiting time");
                }
            }
        } else if (bytesReceived == 0) {
            LogEvent("Server closed connection");
            break;
        } else {
            LogEvent("Error receiving data");
            break;
        }
    }

    // Отправляем благодарность перед выходом
    string thanksMsg = "Client " + to_string(clientId) + ": Thanks for the great service!";
    send(clientSocket, thanksMsg.c_str(), thanksMsg.size(), 0);
    LogEvent("Sent final thanks to server");

    LogEvent("Client " + to_string(clientId) + " finished and disconnecting...");

    // Закрываем соединение
    closesocket(clientSocket);
    WSACleanup();

    LogEvent("Client " + to_string(clientId) + " shutdown complete.");

    // Ожидание нажатия Enter перед выходом
    cout << "Press Enter to exit..." << endl;
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    cin.get();

    return 0;
}
