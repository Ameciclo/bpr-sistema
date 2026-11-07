#ifndef AT_BASE_H
#define AT_BASE_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <vector>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"

class AtBaseState;

class ClientCallbacks : public NimBLEClientCallbacks {
private:
    AtBaseState* atBaseState;

public:
    ClientCallbacks(AtBaseState* state) : atBaseState(state) {}
    void onDisconnect(NimBLEClient* pClient);
};

// PRD lotes/recibos: temporização do protocolo de anúncio
#define BATCH_ANNOUNCE_TIMEOUT_MS 10000
#define MAX_BATCH_ANNOUNCE_RETRIES 3
// Páginas do bitmap de pendências: 64 partes por página (16 hex chars),
// até 8 páginas para 512 partes — cada mensagem cabe em MTU pequeno (T16)
#define BATCH_STATUS_MAX_PAGES 8

class AtBaseState {
private:
    ConfigManager& configManager;
    BufferManager& bufferManager;
    NimBLEClient* pClient;
    NimBLERemoteCharacteristic* pDataChar;
    NimBLERemoteCharacteristic* pConfigChar;
    bool bleConnected;
    unsigned long lastStatusSent;

    // Transferência de partes (uma por vez, ACK da aplicação por parte)
    bool transferActive;
    bool awaitingAck;
    uint16_t fragIndex;   // posição em pendingParts da parte em envio
    uint16_t fragTotal;   // total de partes pendentes desta rodada
    uint8_t fragRetries;
    uint32_t lastFragmentSend;
    bool transferComplete;    // lote limpo: autorização recebida e executada
    bool transferDoneNoClear; // concluído sem autorização (aguardando nuvem)
    bool configRequested;
    uint32_t busyUntilMs;        // R13: espera real após busy

    // ---- PRD lotes/recibos (protocolo BTP1) ----
    bool announced;              // announce enviado nesta conexão
    uint8_t announceRetries;
    uint32_t announceSentAt;
    uint8_t statusBitmap[64];    // pendências acumuladas (512 bits)
    uint8_t statusPagesMask;     // páginas do status já recebidas
    uint8_t statusPagesTotal;
    bool statusComplete;
    char statusState[24];

    std::vector<uint16_t> pendingParts; // índices a enviar (ordem crescente)
    size_t pendingIdx;

    // X02: orçamento global da visita (idle/busy/rejeição têm prazo finito)
    uint32_t visitStart;

    // X08: pedido de config com retry — ACK de config marca received
    bool configReceived;
    uint32_t configRequestTime;
    uint8_t configRetries;
    bool configGaveUp; // X08: desistiu desta conexão; reconexão tenta de novo

    // X09: desconexão da task NimBLE apenas SINALIZA; o estado muda no loop
    volatile bool disconnectSignaled;

    // Notificações BLE -> loop (sem processamento pesado na task do NimBLE)
    QueueHandle_t notifQueue;
    static const size_t NOTIF_MAX_LEN = 516;

    bool connectToBase(NimBLEAdvertisedDevice* device);
    void sendStatus();
    bool sendFragment(uint16_t index);
    void sendConfigAck(bool success, uint32_t version);
    void startTransfer();
    void abortTransfer();
    void processPendingNotifications();

    // Envios/handlers do protocolo de lote (PRD lotes/recibos)
    bool sendBatchAnnounce();
    void sendBatchCleared(uint32_t sid, uint16_t salt, const char shaHex[65]);
    void handleBatchStatus(JsonObject doc);
    void handleClearAuthorization(JsonObject doc);
    void startPartsTransfer();

public:
    AtBaseState(ConfigManager& configMgr, BufferManager& bufferMgr);
    bool scanForBase();
    bool requestConfig();
    BikeState update();
    void onBLEDisconnected();
    void onNotification(const String& message);
    bool isConnected() const;
    bool isTransferring() const { return transferActive; }
    // X09: chamado apenas pela task do NimBLE — não toca estado compartilhado
    void signalDisconnected() { disconnectSignaled = true; }
};

#endif
