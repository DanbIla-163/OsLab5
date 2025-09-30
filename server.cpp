#include <iostream>
#include <string>
#include <winsock2.h>
#include <windows.h>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <map>
#include <vector>

using namespace std;

// Глобальные переменные синхронизации
queue<int> clients;
mutex mtx;
condition_variable cv;
atomic<int> servedCount(0);
atomic<int> totalClientsCreated(0);
atomic<bool> barberSleeping(true);
atomic<bool> isBrotherWorking(false);
atomic<bool> shopOpen(true);

// Карта для хранения сокетов клиентов
map<int, SOCKET> clientSockets;
mutex socketsMtx;

const int MAX_CLIENTS_BEFORE_REST = 4;
const int QUEUE_THRESHOLD_FOR_BROTHER = 2;
const int TOTAL_CLIENTS = 5;

// Фразы для диалога на английском
vector<string> barberPhrases = {
    "Welcome, process ",
    "Hello! How are you today?",
    "What haircut would you like?",
    "Excellent choice!",
    "Let me trim this...",
    "Almost done...",
    "How do you like it?",
    "Thank you, come again!"
};

vector<string> clientPhrases = {
    "Hello, Mr. Barber!",
    "I'd like a fashionable haircut!",
    "Make it shorter in the back",
    "Yes, exactly like that!",
    "Oh, that tickles a bit!",
    "It looks great!",
    "Thanks for the haircut!",
    "I'll definitely come back!"
};

void LogEvent(const string& message) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, message.c_str());
}

void SendToClient(SOCKET clientSocket, const string& message) {
    send(clientSocket, message.c_str(), message.length(), 0);
}

string GetRandomPhrase(const vector<string>& phrases) {
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, phrases.size() - 1);
    return phrases[dis(gen)];
}

void BarberDialogue(SOCKET clientSocket, int clientId, const string& barberName) {
    // Структурированные диалоговые сцены
    vector<pair<string, string>> dialogueScenes = {
        {"Welcome, process " + to_string(clientId) + "!", "Hello, Mr. Barber!"},
        {"How are you today?", "I'm fine, thank you!"},
        {"What haircut would you like?", "I'd like a fashionable haircut!"},
        {"Excellent choice! Let me work on that...", "Great, I'm excited!"},
        {"How does that look?", "It looks amazing!"},
        {"Almost done... just a little more...", "Take your time!"},
        {"All done! How do you like it?", "Perfect! Thank you so much!"},
        {"Thank you, come again!", "I definitely will! Goodbye!"}
    };

    // Проходим по всем сценам диалога
    for (const auto& scene : dialogueScenes) {
        // Парикмахер говорит
        string barberMsg = barberName + ": " + scene.first;
        SendToClient(clientSocket, barberMsg);
        LogEvent(barberMsg);

        this_thread::sleep_for(chrono::milliseconds(2000));

        // Клиент отвечает соответствующей фразой
        string clientResponse = "Client " + to_string(clientId) + ": " + scene.second;
        SendToClient(clientSocket, clientResponse);
        LogEvent(clientResponse);

        this_thread::sleep_for(chrono::milliseconds(1500));

        // Случайная пауза между репликами
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dis(500, 1000);
        this_thread::sleep_for(chrono::milliseconds(dis(gen)));
    }
}
void NotifyClientFinished(int clientId) {
    lock_guard<mutex> lock(socketsMtx);
    auto it = clientSockets.find(clientId);
    if (it != clientSockets.end()) {
        SendToClient(it->second, "HAIRCUT_FINISHED");
        LogEvent("Notified client " + to_string(clientId) + " that haircut is finished");
    }
}

void NotifyClientStarted(int clientId) {
    lock_guard<mutex> lock(socketsMtx);
    auto it = clientSockets.find(clientId);
    if (it != clientSockets.end()) {
        SendToClient(it->second, "HAIRCUT_STARTED");
        LogEvent("Notified client " + to_string(clientId) + " that haircut is started");
    }
}

void AutoCreateClient(int clientId, int port) {
    // Инициализация Winsock для клиента
    WSADATA wsData;
    if (WSAStartup(MAKEWORD(2, 2), &wsData) != 0) {
        LogEvent("Client " + to_string(clientId) + " - WSAStartup failed!");
        return;
    }

    // Создание сокета
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        LogEvent("Client " + to_string(clientId) + " - Socket creation failed!");
        WSACleanup();
        return;
    }

    // Настройка адреса сервера
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Подключение к серверу
    LogEvent("Client " + to_string(clientId) + " connecting to barber shop...");

    int attempts = 0;
    while (attempts < 5) {
        if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == 0) {
            break;
        }
        attempts++;
        this_thread::sleep_for(chrono::milliseconds(500));
    }

    if (attempts >= 5) {
        LogEvent("Client " + to_string(clientId) + " - Connection failed!");
        closesocket(clientSocket);
        WSACleanup();
        return;
    }

    LogEvent("Client " + to_string(clientId) + " successfully connected!");

    // Сохраняем сокет клиента
    {
        lock_guard<mutex> lock(socketsMtx);
        clientSockets[clientId] = clientSocket;
    }

    // Отправляем приветствие и информацию об очереди
    string welcome = "Welcome to Barber Shop! You are client #" + to_string(clientId);
    SendToClient(clientSocket, welcome);

    // Отправляем текущий размер очереди
    {
        lock_guard<mutex> lock(mtx);
        string queueInfo = "Clients in queue before you: " + to_string(clients.size());
        SendToClient(clientSocket, queueInfo);
    }

    // Добавляем в очередь
    {
        lock_guard<mutex> lock(mtx);
        clients.push(clientId);
        LogEvent("Client " + to_string(clientId) + " in queue. Queue size: " + to_string(clients.size()));
        totalClientsCreated++;
    }

    // Будим парикмахера если спит
    if (barberSleeping) {
        cv.notify_one();
    }

    // Ждем сообщения от сервера
    char buffer[1024];
    bool haircutStarted = false;
    bool haircutFinished = false;

    while (!haircutFinished && shopOpen) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            string message = string(buffer);

            if (message == "HAIRCUT_STARTED") {
                LogEvent("Client " + to_string(clientId) + ": My turn! Haircut started!");
                haircutStarted = true;

                // Клиент начинает диалог
                string greeting = "Client " + to_string(clientId) + ": Hello, Mr. Barber!";
                SendToClient(clientSocket, greeting.c_str());
                LogEvent(greeting);

            } else if (message == "HAIRCUT_FINISHED") {
                LogEvent("Client " + to_string(clientId) + ": Haircut finished! Thank you!");
                haircutFinished = true;

                // Финальная благодарность
                string thanksMsg = "Client " + to_string(clientId) + ": Thanks for the great haircut! I'll be back!";
                SendToClient(clientSocket, thanksMsg.c_str());
                LogEvent(thanksMsg);

            } else if (message.find(":") != string::npos) {
                // Это реплика диалога - просто логируем
                LogEvent("DIALOG: " + message);
            }
        } else if (bytesReceived == 0) {
            LogEvent("Client " + to_string(clientId) + ": Server closed connection");
            break;
        } else {
            break;
        }
    }

    // Закрываем соединение
    {
        lock_guard<mutex> lock(socketsMtx);
        clientSockets.erase(clientId);
    }

    closesocket(clientSocket);
    WSACleanup();
    LogEvent("Client " + to_string(clientId) + " disconnected");
}

void CreateClientsAutomatically(int totalClients, int port) {
    LogEvent("Starting automatic creation of " + to_string(totalClients) + " clients...");

    vector<thread> clientThreads;

    for (int i = 1; i <= totalClients; i++) {
        LogEvent("Creating client " + to_string(i) + "...");
        clientThreads.emplace_back(AutoCreateClient, i, port);

        // Задержка между созданием клиентов для формирования очереди
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dis(1000, 3000); // 1-3 секунды
        this_thread::sleep_for(chrono::milliseconds(dis(gen)));

        // Если уже создано достаточно клиентов для тестирования помощника, можно сделать паузу
        if (i == 3) {
            LogEvent("=== Created 3 clients - queue should trigger brother helper ===");
            this_thread::sleep_for(chrono::seconds(2));
        }
    }

    // Ждем завершения всех клиентских потоков
    for (auto& thread : clientThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    LogEvent("All client threads completed");
}

void Barber(bool isMainBarber) {
    string barberName = isMainBarber ? "Main Barber" : "Brother Helper";
    int personalServed = 0;

    LogEvent(barberName + " starts work");

    while (shopOpen && servedCount < TOTAL_CLIENTS) {
        unique_lock<mutex> lock(mtx);

        // Парикмахер спит если нет клиентов
        if (clients.empty()) {
            if (isMainBarber) {
                LogEvent(barberName + " is sleeping...");
                barberSleeping = true;
                cv.wait(lock, [](){ return !clients.empty() || !shopOpen; });
                if (!shopOpen) break;
                barberSleeping = false;
                LogEvent(barberName + " wakes up!");
            } else {
                // Брат ждет недолго, потом уходит
                if (cv.wait_for(lock, chrono::seconds(3), [](){ return !clients.empty() || !shopOpen; })) {
                    if (clients.empty() || !shopOpen) {
                        LogEvent(barberName + " no clients, going back");
                        break;
                    }
                } else {
                    LogEvent(barberName + " timeout, going back");
                    break;
                }
            }
        }

        if (clients.empty()) continue;

        // Берем клиента из очереди
        int clientId = clients.front();
        clients.pop();
        lock.unlock();

        // Получаем сокет клиента для диалога
        SOCKET clientSocket;
        {
            lock_guard<mutex> lock(socketsMtx);
            auto it = clientSockets.find(clientId);
            if (it == clientSockets.end()) {
                continue; // Клиент отключился
            }
            clientSocket = it->second;
        }

        // Уведомляем клиента о начале стрижки
        NotifyClientStarted(clientId);

        // Стрижка с диалогом
        LogEvent(barberName + " starts haircut for client " + to_string(clientId));

        // Начало диалога
        BarberDialogue(clientSocket, clientId, barberName);

        // Случайное время стрижки
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dis(2000, 4000);
        this_thread::sleep_for(chrono::milliseconds(dis(gen)));

        LogEvent(barberName + " finished haircut for client " + to_string(clientId));

        // Уведомляем клиента о завершении стрижки
        NotifyClientFinished(clientId);

        servedCount++;
        personalServed++;

        // Если очередь большая и брат не работает - зовем брата
        {
            lock_guard<mutex> lock(mtx);
            if (clients.size() >= QUEUE_THRESHOLD_FOR_BROTHER && !isBrotherWorking && isMainBarber) {
                LogEvent("Queue is big (" + to_string(clients.size()) + ")! Calling brother helper!");
                isBrotherWorking = true;
                thread brotherThread(Barber, false);
                brotherThread.detach();
            }
        }

        // Отдых после 4 клиентов
        if (personalServed >= MAX_CLIENTS_BEFORE_REST && isMainBarber) {
            LogEvent(barberName + " takes a break after " + to_string(personalServed) + " clients");
            this_thread::sleep_for(chrono::seconds(2));
            personalServed = 0;
        }
    }

    LogEvent(barberName + " finishes work. Served " + to_string(personalServed) + " clients");
    if (!isMainBarber) {
        isBrotherWorking = false;
    }
}

int main() {
    LogEvent("=== Barber Shop Server Starting ===");

    // Инициализация Winsock
    WSADATA wsData;
    if (WSAStartup(MAKEWORD(2, 2), &wsData) != 0) {
        LogEvent("WSAStartup failed!");
        return 1;
    }

    // Создание сокета
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        LogEvent("Socket creation failed!");
        WSACleanup();
        return 1;
    }

    // Настройка адреса
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(54000);

    // Привязка сокета
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        LogEvent("Bind failed! Port 54000 may be in use.");
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // Прослушивание
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        LogEvent("Listen failed!");
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    LogEvent("Barber shop server listening on port 54000...");

    // Запуск главного парикмахера в отдельном потоке
    thread barberThread(Barber, true);

    // Даем парикмахеру время начать работу
    this_thread::sleep_for(chrono::seconds(1));

    // Автоматическое создание клиентов
    CreateClientsAutomatically(TOTAL_CLIENTS, 54000);

    // Завершение работы
    LogEvent("All clients processed, closing shop...");
    shopOpen = false;
    cv.notify_all();

    // Ждем завершения парикмахера
    if (barberThread.joinable()) {
        barberThread.join();
    }

    closesocket(serverSocket);
    WSACleanup();

    LogEvent("=== Barber Shop Server Shutdown ===");
    LogEvent("Total clients created: " + to_string(totalClientsCreated.load()));
    LogEvent("Total clients served: " + to_string(servedCount.load()));

    cout << "Press Enter to exit..." << endl;
    cin.get();

    return 0;
}