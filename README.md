# Clock-Driven Real-Time Executive

### Autore: Samuele Roberto Napolitano  

## 🧠 Descrizione

Questo progetto è stato sviluppato come elaborato finale per il corso di **Sistemi Operativi in Tempo Reale**.  
L'obiettivo è l'implementazione di un **executive clock-driven** in linguaggio C++, capace di schedulare e coordinare task periodici in uno schedule a frame ciclico.

Il sistema è progettato per operare con vincoli real-time sfruttando:
- **Thread permanenti** dedicati a ciascun task
- **Schedulazione ciclica a frame** con sincronizzazione tramite `condition_variable`
- **Misura del WCET** e rilevamento delle deadline miss
- **Degradazione di priorità** e recovery in caso di task in ritardo

---

## 📂 Contenuto del progetto

| File | Descrizione |
|------|-------------|
| `executive.h` / `executive.cpp` | Executive real-time clock-driven |
| `application_1.cpp` | Applicazione con 5 task periodici schedulati ciclicamente |
| `busy_wait.h` | Funzione di attesa attiva (simula carico CPU) |
| `README.md` | Descrizione completa del progetto |
| `README.txt` | File testuale per la consegna |

---

## ⚙️ Requisiti

- Compilatore **g++** con supporto `-std=c++11` o superiore
- Sistema operativo Linux
- **Permessi root** per eseguire con priorità real-time e impostare l’affinità ai core

---

## 🚀 Compilazione e Esecuzione

```bash
make                 # oppure g++ -pthread -O3 -std=c++11 -o application_1 application_1.cpp executive.cpp busy_wait.cpp
sudo ./application_1

