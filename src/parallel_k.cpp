#include <fstream>
#include <iostream>
#include <string>
#include <omp.h>

using namespace std; // Permite usar cout, string, etc. sin escribir std::

struct Punto {
    double x;
    double y;
    int asignacion = -1; // Clasificación final del punto dentro de DBSCAN

    // Posición espacial del punto dentro de la cuadrícula de bloques
    int fila_bloque = -1;
    int columna_bloque = -1;
};

struct Bloque {
    // Límites espaciales del bloque
    double min_x;
    double max_x;
    double min_y;
    double max_y;

    // Índices de los puntos originales que pertenecen a este bloque
    int* indices_ptos;

    // Número total de puntos que pertenecen al bloque
    int cantidad = 0;

    // Número de posiciones del arreglo indices_ptos que ya fueron llenadas
    int insertados = 0;
};

// Lee el archivo CSV y guarda todos los puntos en un arreglo dinámico
Punto* leer_csv(string nombreArchivo, int n){
    ifstream archivo(nombreArchivo);

    if(!archivo.is_open()) {
        return nullptr;
    }

    Punto* puntos = new Punto[n]; // Arreglo principal de puntos
    double x, y;
    char coma;

    for (int i = 0; i < n; i++){
        archivo >> x >> coma >> y;
        puntos[i].x = x;
        puntos[i].y = y;
    }

    archivo.close();
    return puntos;
}

// Guarda en CSV las coordenadas y la asignación final de cada punto
int guardar_csv(string nombreArchivo, int n, Punto* puntos){
    ofstream archivo(nombreArchivo); // La carpeta data/output/ ya debe existir

    if(!archivo.is_open()){
        cout << "Error al crear archivo de salida";
        return 1;
    }

    for (int i = 0; i < n; i++){
        archivo << puntos[i].x << ","
                << puntos[i].y << ","
                << puntos[i].asignacion << "\n";
    }

    archivo.close();
    return 0;
}

// Cambia la asignación de un punto a 1 de forma segura cuando varios hilos pueden acceder al mismo punto
void marcar_en_cluster(Punto* puntos, int indice){
    int valor_actual; // Aquí guardamos temporalmente el valor de asignacion

    // Lectura atómica: evita que otro hilo interfiera durante esta lectura
    #pragma omp atomic read
    valor_actual = puntos[indice].asignacion;

    // Solo escribimos si el punto todavía no había sido marcado
    if(valor_actual != 1){

        // Escritura atómica: evita una carrera entre hilos sobre asignacion
        #pragma omp atomic write
        puntos[indice].asignacion = 1;
    }
}

// Calcula los límites globales del conjunto de puntos:
// [min_x, max_x, min_y, max_y]
double* limit_build(Punto* puntos, int n){
    double max_x = puntos[0].x;
    double min_x = puntos[0].x;
    double max_y = puntos[0].y;
    double min_y = puntos[0].y;

    // Recorremos todos los puntos buscando mínimos y máximos
    for(int i = 1; i < n; i++){

        if(puntos[i].x > max_x){
            max_x = puntos[i].x;
        }

        if(puntos[i].x < min_x){
            min_x = puntos[i].x;
        }

        if(puntos[i].y > max_y){
            max_y = puntos[i].y;
        }

        if(puntos[i].y < min_y){
            min_y = puntos[i].y;
        }
    }

    // Guardamos los cuatro límites en un arreglo
    double* limites = new double[4];

    limites[0] = min_x;
    limites[1] = max_x;
    limites[2] = min_y;
    limites[3] = max_y;

    return limites;
}

// Divide el espacio completo en una cuadrícula de bloques
// y asigna cada punto al bloque espacial al que pertenece
Bloque** construir_particiones(Punto* puntos, int n, int filas, int columnas, double* limites){

    // Ancho de cada bloque sobre el eje x
    double long_part_x = (limites[1] - limites[0]) / columnas;

    // Altura de cada bloque sobre el eje y
    double long_part_y = (limites[3] - limites[2]) / filas;

    // Creamos un arreglo de punteros, uno por cada fila
    Bloque** bloques = new Bloque*[filas];

    // Cada fila apunta a un arreglo de bloques, uno por cada columna
    for(int fila = 0; fila < filas; fila++){
        bloques[fila] = new Bloque[columnas];
    }

    /*
        Geométricamente:

        y
        ↑

        +---------+---------+---------+
        | B20     | B21     | B22     |
        +---------+---------+---------+
        | B10     | B11     | B12     |
        +---------+---------+---------+
        | B00     | B01     | B02     |
        +---------+---------+---------+ → x

        bloques[fila][columna]
    */

    // Calculamos los límites espaciales de cada bloque
    for(int fila = 0; fila < filas; fila++){

        for(int columna = 0; columna < columnas; columna++){

            bloques[fila][columna].min_x =
                limites[0] + long_part_x * columna;

            bloques[fila][columna].max_x =
                limites[0] + long_part_x * (columna + 1);

            bloques[fila][columna].min_y =
                limites[2] + long_part_y * fila;

            bloques[fila][columna].max_y =
                limites[2] + long_part_y * (fila + 1);
        }
    }

    // Primera pasada:
    // determinamos en qué bloque cae cada punto y contamos cuántos puntos tendrá cada bloque
    for(int i = 0; i < n; i++){

        // La coordenada x determina la columna
        int columna =
            (puntos[i].x - limites[0]) / long_part_x;

        // La coordenada y determina la fila
        int fila =
            (puntos[i].y - limites[2]) / long_part_y;

        // Si el punto está exactamente en max_x,
        // la fórmula daría columna == columnas, que estaría fuera del arreglo
        if(columna == columnas){
            columna = columnas - 1;
        }

        // Mismo caso para el límite máximo de y
        if(fila == filas){
            fila = filas - 1;
        }

        // Aumentamos el número de puntos que tendrá ese bloque
        bloques[fila][columna].cantidad++;

        // Guardamos en el propio punto su posición dentro de la cuadrícula
        puntos[i].fila_bloque = fila;
        puntos[i].columna_bloque = columna;
    }

    // Ahora que sabemos cuántos puntos tendrá cada bloque,
    // reservamos exactamente el espacio necesario para sus índices
    for(int fila = 0; fila < filas; fila++){

        for(int columna = 0; columna < columnas; columna++){

            bloques[fila][columna].indices_ptos =
                new int[bloques[fila][columna].cantidad];
        }
    }

    // Segunda pasada:
    // guardamos en cada bloque los índices de los puntos originales que le pertenecen
    for(int i = 0; i < n; i++){

        int fila = puntos[i].fila_bloque;
        int columna = puntos[i].columna_bloque;

        // Posición disponible dentro del arreglo de índices del bloque
        int posicion = bloques[fila][columna].insertados;

        // Guardamos i, no una copia del punto.
        // Así podemos acceder después al punto original con puntos[i]
        bloques[fila][columna].indices_ptos[posicion] = i;

        // Indicamos que esa posición ya fue ocupada
        bloques[fila][columna].insertados++;
    }

    return bloques;
}

int main(int argc, char** argv) {
    
    // Ejemplo de ejecución:
    //
    // ./parallel_k 20000 0.03 10 4 8
    //
    // argv[0] = "./parallel_k"
    // argv[1] = "20000"   -> número de puntos
    // argv[2] = "0.03"    -> epsilon
    // argv[3] = "10"      -> mínimo de puntos
    // argv[4] = "4"       -> número de hilos
    // argv[5] = "8"       -> número de particiones k
    //
    // argc = 6


    // Verificamos que se hayan recibido todos los argumentos necesarios
    if (argc != 6) {
        cout << "Uso: ./parallel_k n epsilon min_puntos num_hilos k";
        return 1;
    }


    // -------------------------
    // Parámetros del algoritmo
    // -------------------------

    int n = stoi(argv[1]);
    double epsilon = stod(argv[2]);
    int min_puntos = stoi(argv[3]);


    // -------------------------
    // Configuración de OpenMP
    // -------------------------

    int num_hilos = stoi(argv[4]);

    if (num_hilos < 1) {
        cout << "num_hilos debe ser al menos 1";
        return 1;
    }

    omp_set_num_threads(num_hilos);


    // -------------------------
    // Número de particiones
    // -------------------------

    int k = stoi(argv[5]);


    // -------------------------
    // Archivos de entrada/salida
    // -------------------------

    string nombreEntrada =
        "data/input/" + to_string(n) + "_data.csv";

    string nombreSalida =
        "data/output/parallel_k/" +
        to_string(n) +
        "_results_parallel_k.csv";


    // -------------------------
    // Lectura de los puntos
    // -------------------------

    // Se lee todo el CSV y se guarda en un único arreglo.
    // Este arreglo "puntos" será el arreglo principal durante todo el programa.
    Punto* puntos = leer_csv(nombreEntrada, n);

    if (puntos == nullptr) {
        cout << "Error al leer el archivo";
        return 1;
    }


    // =========================================================
    // CONSTRUCCIÓN DE LAS PARTICIONES ESPACIALES
    // =========================================================


    // Obtenemos los límites globales de todos los puntos:
    //
    // limites[0] = min_x
    // limites[1] = max_x
    // limites[2] = min_y
    // limites[3] = max_y
    double* limites = limit_build(puntos, n);


    // ---------------------------------------------------------
    // Determinar cuántas filas y columnas tendrá la cuadrícula
    // ---------------------------------------------------------
    //
    // Empezamos con una sola región:
    //
    // 1 x 1
    //
    // y vamos duplicando alternativamente columnas y filas.
    //
    // k = 2  -> 1 x 2
    // k = 4  -> 2 x 2
    // k = 8  -> 2 x 4
    // k = 16 -> 4 x 4
    // k = 32 -> 4 x 8
    // k = 64 -> 8 x 8

    int filas = 1;
    int columnas = 1;

    int p = 0;

    while (filas * columnas != k) {

        if (p % 2 == 0) {
            columnas *= 2;
        }
        else {
            filas *= 2;
        }

        p++;
    }


    // ---------------------------------------------------------
    // Construir los bloques
    // ---------------------------------------------------------
    //
    // construir_particiones:
    //
    // 1. Divide el espacio entre min_x, max_x, min_y y max_y.
    // 2. Crea una matriz bloques[fila][columna].
    // 3. Determina a qué bloque pertenece cada punto.
    // 4. Guarda en cada bloque los índices de sus puntos.
    //
    // Importante:
    // los bloques NO guardan copias de los puntos.
    // Guardan índices que hacen referencia al arreglo original "puntos".

    Bloque** bloques =
        construir_particiones(
            puntos,
            n,
            filas,
            columnas,
            limites
        );


    // =========================================================
    // PREPARACIÓN DEL CÁLCULO DBSCAN
    // =========================================================


    // Como todas las distancias se compararán contra epsilon,
    // usamos epsilon² para evitar calcular sqrt().
    double epsilon_cuadrado = epsilon * epsilon;


    // =========================================================
    // MEDICIÓN DE TIEMPO
    // =========================================================
    //
    // omp_get_wtime() mide tiempo real transcurrido.
    //
    // El cronómetro empieza después de:
    // - leer el CSV
    // - calcular los límites
    // - construir las particiones
    //
    // De esta manera medimos principalmente el algoritmo paralelo.

    double tiempo_inicio = omp_get_wtime();


    // =========================================================
    // A PARTIR DE AQUÍ COMIENZA EL ALGORITMO PARALELO
    // =========================================================
    //
    // Todo lo creado hasta este punto:
    //
    // puntos
    // bloques
    // limites
    // epsilon_cuadrado
    //
    // existe antes de entrar a la región paralela.
    //
    // Después tendremos que decidir qué variables:
    // - son compartidas entre hilos
    // - necesita cada hilo de forma privada

    #pragma omp parallel
    {
        //lista de vecinos PRIVADA de cada hilo: cada hilo crea su propio arreglo de n enteros
        // (La lista tiene tamaño n porque, en el peor caso, un punto tiene n-1 vecinos.)
        
        int* indices_vecinos = new int[n]; //Esto lo hacen todos los hilos una sola vez

        //for paralelo: va a repartir entre hilos. Va a haber un y solo un i por hilo.
        //todos los hilos deben acabar su parte antes de que pueda terminar este for
        #pragma omp for
        for(int i=0; i<n; i++){ //Esto es lo que estamos paralelizando !!!!

            //cada hilo tiene su propio num vecinos. se reinicia en 0 con cada punto i
            int num_vecinos=0;

            //Este for de j ya no esta paralelizado. Cada hilo recorre todos los j para su i
            for(int j=0; j<n; j++){

                if(i!=j){ // Un punto no se compara contra si mismo

                    //privadas de cada hilo
                    // (Es la misma cuenta que el serial; solo se escribe con variables intermedias para que se lea mejor.)
                    double dx = puntos[j].x - puntos[i].x;
                    double dy = puntos[j].y - puntos[i].y;
                    double distancia_cuadrada = dx*dx + dy*dy;

                    if(distancia_cuadrada <= epsilon_cuadrado){ // entra en vecino?
                        indices_vecinos[num_vecinos]=j; //guarda j en la lista privada de vecinos de i de este hilo en especifico
                        num_vecinos++;
                    }
                }

            }

            //+1 cuenta a i, y si i tuvo al menos min puntos vecinos, i es nucelo
            if(num_vecinos+1>=min_puntos){

                //Aqui en el serial era "puntos[i].asignacion=1;". Ahora usamos atomica
                // porque otro hilo podria estar marcando este mismo punto (como vecino de otro nucleo).
                marcar_en_cluster(puntos, i);

                // Todos los vecinos de un nucleo pertenecen al cluster (aunque ellos no sean nucleo: son puntos en borde)
                // NUEVO: se usa k como contador (en el serial se reutilizaba j), y marcar es atoomico
                //cambiamos a k solo para leer mejor; para no re-utilizar j, auqneu igual funciona
                for(int k=0; k<num_vecinos; k++){

                    marcar_en_cluster(puntos, indices_vecinos[k]);

                }

            }
        } //termina omp for. Todos los hilos deben terminar antes de seguir. Van a acabar al mismo tiempo 
        // masomenos porque se reparte equitativamente y tienen masomenos la misma cantidad de puntos

        delete[] indices_vecinos; // c/u de los hilos libera su lista privada

    }

    // Termina region paralela ! 
    // Nos pasamos a un unico hilo que tiene acceso a lo que hicieron todos los mini hilos

    //detenemos cronometro
    double tiempo_fin = omp_get_wtime();

    // NUEVO: omp_get_max_threads() regresa cuántos hilos usa OpenMP en una región paralela.
    //para saber con cuántos hilos se hizo cada corrida y para confirmar que tu 4to argumento sí se aplicó.
    cout << "TIEMPO=" << (tiempo_fin - tiempo_inicio) << "\n";

    int resultado = guardar_csv(nombreSalida, n, puntos); // La escritura del archivo también es secuencial

    // Limpieza de memoria de los arreglos utilizados
    delete[] limites; 
    delete[] puntos; 

    return resultado; // 0 si todo salió bien, 1 si falló al guardar



    // Para correr el archivo:
    // 1. Se compila el script (¡con -fopenmp!):
    //      g++ -O2 -std=c++17 -fopenmp src/parallel_full.cpp -o src/parallel_full
    // 2. Se ejecuta en la terminal, desde la carpeta raíz del proyecto:
    //      ./src/parallel_full n epsilon min_puntos [num_hilos]
    //    Ejemplo:  ./src/parallel_full 20000 0.03 10 4

}