//Maria Amélia Doná Aguilar
//Jane Harumi Yamamoto
//Data: 06/12/2024
//Trabalho de Sistemas Operacionais
//Professor: Joao Carlos de Moraes Morselli Junior
//Tema: Restaurante
//Descrição: O programa simula um sistema de restaurante com um atendente e uma cozinha.
//O atendente recebe os pedidos do cliente e os envia para a cozinha, que simula o preparo dos pedidos.
//Ao finalizar o preparo, a cozinha envia o pedido pronto de volta para o atendente, que então informa ao cliente.
//Além disso, o programa calcula e exibe estatísticas como o tempo médio de preparo dos pedidos.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>

// Definições de constantes
#define MAX_DESCRICAO 100 // Tamanho máximo da descrição de um pedido
#define TEMPO_PREPARO 2   // Tempo de preparo simulado em segundos
#define FIFO_ATENDENTE "/tmp/fifo_atendente" // FIFO para comunicação Atendente -> Cozinha
#define FIFO_COZINHA "/tmp/fifo_cozinha"     // FIFO para comunicação Cozinha -> Atendente

// Estrutura que representa um pedido
typedef struct {
    int id;                      // ID único do pedido
    char descricao[MAX_DESCRICAO]; // Descrição do pedido
    time_t horaPedido;            // Horário em que o pedido foi feito
    time_t horaPronto;            // Horário em que o pedido ficou pronto
} Pedido;

// Variáveis globais para FIFOs e controle
int fdAtendente = -1; // Descritor do FIFO para escrita pelo atendente
int fdCozinha = -1;   // Descritor do FIFO para leitura pela cozinha

// Mutexes para controle de concorrência
pthread_mutex_t mutex_atendente = PTHREAD_MUTEX_INITIALIZER; // Protege acessos ao FIFO do atendente
pthread_mutex_t mutex_cozinha = PTHREAD_MUTEX_INITIALIZER;   // Protege acessos ao FIFO da cozinha
pthread_mutex_t mutex_stats = PTHREAD_MUTEX_INITIALIZER;     // Protege variáveis de estatísticas

// Variáveis para estatísticas
int total_pedidos = 0;           // Contador de pedidos recebidos
int pedidos_processados = 0;     // Contador de pedidos concluídos

// Adicionar uma variável global para controle de encerramento
volatile sig_atomic_t programa_executando = 1;

// Função para limpar FIFOs ao encerrar o programa
void limpar_fifos() {
    if (fdAtendente != -1) close(fdAtendente); // Fecha o FIFO de escrita se aberto
    if (fdCozinha != -1) close(fdCozinha);     // Fecha o FIFO de leitura se aberto
    unlink(FIFO_ATENDENTE); // Remove FIFO do sistema de arquivos
    unlink(FIFO_COZINHA);
}

// Modificar a função signal_handler
void signal_handler(int signum) {
    printf("\nRecebido sinal de interrupção. Iniciando encerramento seguro...\n");
    programa_executando = 0;  // Sinaliza para o programa encerrar
}

// Função executada pelo processo filho (Cozinha)
void cozinha() {
    Pedido pedido;

    printf("[Cozinha] Aguardando conexão com os pipes...\n");

    // Abrir o FIFO para leitura (recebe pedidos do atendente)
    fdAtendente = open(FIFO_ATENDENTE, O_RDONLY);
    if (fdAtendente < 0) {
        perror("[Cozinha] Erro ao abrir FIFO_ATENDENTE");
        exit(EXIT_FAILURE);
    }

    // Abrir o FIFO para escrita (envia pedidos prontos para o atendente)
    fdCozinha = open(FIFO_COZINHA, O_WRONLY);
    if (fdCozinha < 0) {
        perror("[Cozinha] Erro ao abrir FIFO_COZINHA");
        close(fdAtendente);
        exit(EXIT_FAILURE);
    }

    printf("[Cozinha] Pronta para receber pedidos.\n");

    // Loop para processar pedidos
    while (programa_executando) {  // Verifica a flag de encerramento
        pthread_mutex_lock(&mutex_atendente);
        ssize_t bytes_read = read(fdAtendente, &pedido, sizeof(Pedido));
        pthread_mutex_unlock(&mutex_atendente);

        if (bytes_read > 0) {
            // Verifica se o comando foi para encerrar
            if (strcmp(pedido.descricao, "sair") == 0) {
                printf("[Cozinha] Encerrando operações...\n");
                break;
            }

            printf("[Cozinha] Recebido pedido #%d: %s\n", pedido.id, pedido.descricao);

            // Simula o preparo do pedido
            sleep(TEMPO_PREPARO);

            time(&pedido.horaPronto); // Marca o horário em que o pedido ficou pronto
            printf("[Cozinha] Pedido #%d pronto!\n", pedido.id);

            // Envia o pedido pronto para o atendente
            pthread_mutex_lock(&mutex_cozinha);
            write(fdCozinha, &pedido, sizeof(Pedido));
            pthread_mutex_unlock(&mutex_cozinha);

            // Atualiza estatísticas
            pthread_mutex_lock(&mutex_stats);
            pedidos_processados++;
            pthread_mutex_unlock(&mutex_stats);
        } else if (bytes_read == 0) {
            printf("[Cozinha] Pipe fechado pelo atendente.\n");
            break;
        }
    }

    // Fecha os FIFOs antes de encerrar
    close(fdAtendente);
    close(fdCozinha);
    exit(EXIT_SUCCESS);
}

// Modificar a função monitorar_pedidos
void *monitorar_pedidos(void *arg) {
    int fdCozinha = *(int *)arg;
    Pedido pedido;

    while (programa_executando) {  // Verifica a flag de encerramento
        pthread_mutex_lock(&mutex_cozinha);
        ssize_t bytes_read = read(fdCozinha, &pedido, sizeof(Pedido));
        pthread_mutex_unlock(&mutex_cozinha);

        if (bytes_read > 0) {
            double tempoPreparo = difftime(pedido.horaPronto, pedido.horaPedido);
            printf("[Monitor] Pedido #%d pronto! (Tempo de preparo: %.2f segundos)\n",
                   pedido.id, tempoPreparo);
        } else if (bytes_read == 0) {
            printf("[Monitor] Pipe fechado.\n");
            break;
        }
    }
    return NULL;
}

// Função principal
int main() {
    // Configurar tratadores de sinais
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Remover FIFOs antigos, se existirem
    unlink(FIFO_ATENDENTE);
    unlink(FIFO_COZINHA);

    // Criar os FIFOs para comunicação
    if (mkfifo(FIFO_ATENDENTE, 0666) < 0) {
        perror("Erro ao criar FIFO_ATENDENTE");
        return EXIT_FAILURE;
    }

    if (mkfifo(FIFO_COZINHA, 0666) < 0) {
        perror("Erro ao criar FIFO_COZINHA");
        unlink(FIFO_ATENDENTE);
        return EXIT_FAILURE;
    }

    printf("[Atendente] FIFOs criados com sucesso.\n");

    // Criar processo filho (Cozinha)
    pid_t pid = fork();
    if (pid < 0) {
        perror("Erro ao criar processo filho");
        limpar_fifos();
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        // Executa o processo filho (Cozinha)
        cozinha();
        return 0;
    }

    // Código do processo pai (Atendente)
    printf("[Atendente] Abrindo FIFOs...\n");

    // Abrir os FIFOs
    fdAtendente = open(FIFO_ATENDENTE, O_WRONLY);
    fdCozinha = open(FIFO_COZINHA, O_RDONLY);

    // Verificar erros na abertura dos FIFOs
    if (fdAtendente < 0 || fdCozinha < 0) {
        perror("[Atendente] Erro ao abrir FIFOs");
        limpar_fifos();
        return EXIT_FAILURE;
    }

    // Criar thread para monitorar pedidos prontos
    pthread_t monitorThread;
    if (pthread_create(&monitorThread, NULL, monitorar_pedidos, &fdCozinha) != 0) {
        perror("[Atendente] Erro ao criar thread de monitoramento");
        limpar_fifos();
        return EXIT_FAILURE;
    }

    printf("[Atendente] Sistema pronto para receber pedidos!\n\n");
    printf("\n=== Sistema de Pedidos do Restaurante ===\n");
    printf("Instruções:\n");
    printf("- Digite o pedido desejado e pressione Enter\n");
    printf("- Digite 'sair' para encerrar o programa\n\n");

    // Loop para receber pedidos do usuário
    Pedido pedido;
    int id_pedido = 1;
    char input[MAX_DESCRICAO];
    while (programa_executando) {  // Verifica a flag de encerramento
        printf("Digite o pedido #%d: ", id_pedido);
        if (fgets(input, sizeof(input), stdin) == NULL || !programa_executando) {
            break;
        }
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "sair") == 0 || !programa_executando) {
            strcpy(pedido.descricao, "sair");
            pthread_mutex_lock(&mutex_atendente);
            write(fdAtendente, &pedido, sizeof(Pedido));
            pthread_mutex_unlock(&mutex_atendente);
            break;
        }

        // Prepara o pedido
        pedido.id = id_pedido++;
        strcpy(pedido.descricao, input);
        time(&pedido.horaPedido);

        // Envia o pedido para a cozinha (protegido por mutex)
        pthread_mutex_lock(&mutex_atendente);
        write(fdAtendente, &pedido, sizeof(Pedido));
        pthread_mutex_unlock(&mutex_atendente);

        // Atualiza estatísticas (protegido por mutex)
        pthread_mutex_lock(&mutex_stats);
        total_pedidos++;
        pthread_mutex_unlock(&mutex_stats);
    }

    // Encerramento seguro
    printf("\nIniciando encerramento do sistema...\n");

    // Envia sinal de encerramento para a cozinha se ainda não foi enviado
    strcpy(pedido.descricao, "sair");
    pthread_mutex_lock(&mutex_atendente);
    write(fdAtendente, &pedido, sizeof(Pedido));
    pthread_mutex_unlock(&mutex_atendente);

    // Aguarda a thread de monitoramento
    pthread_cancel(monitorThread);
    pthread_join(monitorThread, NULL);

    // Exibe estatísticas finais
    printf("\nEstatísticas finais:\n");
    printf("Total de pedidos: %d\n", total_pedidos);
    printf("Pedidos processados: %d\n", pedidos_processados);

    // Destruição dos mutexes e limpeza
    pthread_mutex_destroy(&mutex_atendente);
    pthread_mutex_destroy(&mutex_cozinha);
    pthread_mutex_destroy(&mutex_stats);

    limpar_fifos();
    printf("[Atendente] Sistema encerrado com sucesso.\n");
    return 0;
}
