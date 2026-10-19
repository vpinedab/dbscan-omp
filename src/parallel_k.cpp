//Notas:

// Dividimos en k particiones. Los hilos se van a repartir estos bloques de manera dinamica.
// Creamos un HALO: incluye a todos los vecinos a epsilon distancia (o menos)de un punto, sin importar que 
// esten en otro bloque !!!
// Todos los bloques (hilos) Escriben sobre un único arreglo de puntos usando operaciones atomicas,
// cuando todos acaban el hilo principal ve esto y ya tiene el ruido. 

#include <fstream>
#include <iostream>
#include <string>
#include <omp.h>

#include <cmath> // Librería para utilizar ceil()

using namespace std;

struct Punto {
    double x;
    double y;
    int asignacion = -1;

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

// Lee el archivo CSV
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
    ofstream archivo(nombreArchivo);

    if(!archivo.is_open()){
        cout << "Error al crear archivo de salida\n";
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

//De nuevo el atomic para evitar condicion de carrera

void marcar_en_cluster(Punto* puntos, int indice){
    int valor_actual; 
    #pragma omp atomic read
    valor_actual = puntos[indice].asignacion;

    if(valor_actual != 1){

        #pragma omp atomic write
        puntos[indice].asignacion = 1;
    }
}

// límites globales del conjunto:
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
Bloque** construir_particiones(Punto* puntos, int n, int filas, int columnas, double long_part_x, double long_part_y, double* limites){

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

    // Ahora que ya sabemos cuántos puntos tendrá cada bloque, reservamos espacio 
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

// Libera toda la memoria que reservó construir_particiones.
// Se libera "de adentro hacia afuera", en el orden inverso a como se reservó:
//   1. la lista de índices de cada bloque
//   2. el arreglo de bloques de cada fila
//   3. el arreglo de filas
void liberar_particiones(Bloque** bloques, int filas, int columnas){

    for(int fila = 0; fila < filas; fila++){

        for(int columna = 0; columna < columnas; columna++){

            delete[] bloques[fila][columna].indices_ptos; // 1. lista de índices de este bloque
        }

        delete[] bloques[fila]; // 2. los bloques de esta fila
    }

    delete[] bloques; // 3. el arreglo de filas
}

// Reúne en el arreglo `candidatos` los índices de TODOS los puntos que un bloque necesita
// mirar para contar bien los vecinos de sus puntos. Regresa cuántos candidatos reunió.
//
//   1. Los puntos del propio bloque (van PRIMERO en la lista).
//   2. El HALO: puntos que viven en OTROS bloques pero están a menos de epsilon del borde de este.
//
// Parámetros:
//   puntos            arreglo principal de puntos (compartido por todos los hilos, aquí solo se lee)
//   n                 cantidad total de puntos
//   bloques           la cuadrícula de bloques
//   fila_b, columna_b qué bloque estamos preparando
//   filas, columnas   dimensiones de la cuadrícula
//   margen            cuánto agrandamos el rectángulo del bloque para formar el halo (ver main)
//   long_part_x       ancho de cada bloque
//   long_part_y       alto de cada bloque
//   candidatos        lista donde se guardan los índices (cada hilo pasa la SUYA, no se comparte)

int reunir_candidatos(Punto* puntos, int n, Bloque** bloques,
                      int fila_b, int columna_b, 
                      int filas, int columnas,
                      double margen, 
                      double long_part_x, double long_part_y,
                      int* candidatos){

    // "bloque" es otro nombre para el mismo bloques[fila_b][columna_b] (una referencia: no copia nada).
    // Solo sirve para no escribir esa expresión larga cada vez.
    Bloque& bloque = bloques[fila_b][columna_b];

    int num_candidatos = 0; // Cuántos candidatos llevamos reunidos

    // PASO 1: los puntos del propio bloque van primero.
    for(int pos = 0; pos < bloque.cantidad; pos++){

        candidatos[num_candidatos] = bloque.indices_ptos[pos];
        num_candidatos++;
    }

    // PASO 2: el halo.
    // Es el rectángulo del bloque, agrandado `margen` hacia cada lado.
    double halo_min_x = bloque.min_x - margen;
    double halo_max_x = bloque.max_x + margen;
    double halo_min_y = bloque.min_y - margen;
    double halo_max_y = bloque.max_y + margen;

    // PASO 3: Determinar cuántas filas y columnas revisar
    // En el caso especial de que la longitud del bloque sea menor al epsilon/margen 
    // hay que verificar cuántos bloques más tenemos que revisar
    int rango_columnas = ceil(margen/long_part_x);
    int rango_filas = ceil(margen/long_part_y);

    // PASO 4: Limitar la búsqueda a los bordes de la cuadrícula

    int columna_inicio = columna_b - rango_columnas;
    int columna_fin = columna_b + rango_columnas;

    int fila_inicio = fila_b - rango_filas;
    int fila_fin = fila_b + rango_filas;

    if(columna_inicio < 0){
        columna_inicio = 0;
    }

    if(columna_fin >= columnas){
        columna_fin = columnas - 1;
    }

    if(fila_inicio < 0){
        fila_inicio = 0;
    }

    if(fila_fin >= filas){
        fila_fin = filas - 1;
    }

    // PASO 5: Recorrer los bloques cercanos

    for(int fila=fila_inicio; fila<=fila_fin; fila++){
        for(int columna=columna_inicio; columna<=columna_fin; columna++){
            
            // Los del propio bloque ya se agregaron en el paso 1
            if(fila==fila_b && columna==columna_b){
                continue;
            }

            Bloque& vecino = bloques[fila][columna];

            // Revisar únicamente los puntos que pertenecen al bloque vecino
            for(int pos = 0; pos < vecino.cantidad; pos++){

                int j = vecino.indices_ptos[pos];

                // Si es de otro bloque y cae dentro del rectángulo agrandado, forma parte del halo
                if(puntos[j].x >= halo_min_x && puntos[j].x <= halo_max_x && puntos[j].y >= halo_min_y && puntos[j].y <= halo_max_y){
                    candidatos[num_candidatos] = j;
                    num_candidatos++;
                }
            }
        }
    }

    return num_candidatos;
}

void detectar_ruido_parallel_k(Punto* puntos, int n, Bloque** bloques, int k, int filas, int columnas, double margen, double long_part_x, double long_part_y, double epsilon_cuadrado, int min_puntos){

    #pragma omp parallel
    {
        // Listas PRIVADAS de cada hilo: cada hilo crea sus propios arreglos de n enteros.
        // (Tamaño n porque, en el peor caso, todos los puntos son candidatos o vecinos.)
        int* candidatos = new int[n];       
        int* indices_vecinos = new int[n];  

        //Aqui usamos asignación dinamica porque si un bloque casi no tiene casi puntos, va a cabar mucho antes que
        //uno con muchos puntos. 
        #pragma omp for schedule(dynamic)
        for(int b = 0; b < k; b++){

            // Los bloques se numeran de 0 a k-1 recorriendo la cuadrícula fila por fila.
            // Para saber qué bloque es b: la división entera da la fila y el residuo da la columna.
            // Ejemplo con una cuadrícula de 2 filas x 4 columnas: b = 5 -> fila 5/4 = 1, columna 5%4 = 1.
            int fila_b = b / columnas;
            int columna_b = b % columnas;

            int cantidad_propios = bloques[fila_b][columna_b].cantidad; // Puntos que viven en este bloque

            // Un bloque vacío no tiene nada que analizar (y así evitamos reunir su halo para nada).
            // "continue" salta a la siguiente iteración del for.
            if(cantidad_propios == 0){
                continue;
            }

            // Reunimos los candidatos del bloque: sus puntos (primero) + su halo.
            int num_candidatos = reunir_candidatos(puntos, n, bloques, fila_b, columna_b, filas, columnas, margen, long_part_x, long_part_y, candidatos);

            // Analizamos uno por uno los puntos PROPIOS del bloque: son las primeras
            // `cantidad_propios` posiciones de la lista de candidatos.
            for(int pos = 0; pos < cantidad_propios; pos++){

                int i = candidatos[pos]; // Índice del punto que vamos a analizar

                // Copiamos sus coordenadas a variables locales: se usan miles de veces en el ciclo de abajo.
                double xi = puntos[i].x;
                double yi = puntos[i].y;

                int num_vecinos = 0; // Se reinicia con cada punto i, igual que en el serial

                // CAMBIO: en el serial (y en la versión full) j recorría los n puntos.
                // Ahora solo recorremos los candidatos: los del bloque y su halo.
                for(int c = 0; c < num_candidatos; c++){

                    int j = candidatos[c]; // Índice real del punto candidato

                    if(i != j){ // Un punto no se compara contra sí mismo

                        double dx = puntos[j].x - xi;
                        double dy = puntos[j].y - yi;
                        double distancia_cuadrada = dx*dx + dy*dy; // Misma cuenta que en el serial

                        if(distancia_cuadrada <= epsilon_cuadrado){ // ¿j es vecino de i?
                            indices_vecinos[num_vecinos] = j; // Se guarda en la lista privada de este hilo
                            num_vecinos++;
                        }
                    }
                }

                // +1 cuenta a i; si i tiene al menos min_puntos puntos en su vecindad, i es núcleo
                if(num_vecinos + 1 >= min_puntos){

                    // Se marca con la versión atómica, porque otro hilo podría estar marcando este mismo punto
                    // (como vecino de un núcleo de otro bloque, por ejemplo).
                    marcar_en_cluster(puntos, i);

                    // Todos los vecinos de un núcleo pertenecen al cluster (aunque ellos no sean núcleo:
                    // son puntos de borde). Un vecino puede vivir en OTRO bloque (viene del halo): aquí es
                    // donde se UNEN los resultados de los distintos bloques, porque la marca queda en el
                    // arreglo compartido y la verán todos.
            
                    for(int v = 0; v < num_vecinos; v++){

                        marcar_en_cluster(puntos, indices_vecinos[v]);
                    }
                }
            }
        } // Termina el omp for: barrera. Ningún hilo sigue hasta que TODOS los bloques estén listos.

        delete[] candidatos;       // Cada hilo libera sus listas privadas
        delete[] indices_vecinos;
    }
}

int main(int argc, char** argv) {
/*
//  EJECUCIÓN NORMAL

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
    if (argc != 6) {
        cout << "Uso: ./parallel_k n epsilon min_puntos num_hilos k\n";
        return 1;
    }

    int n = stoi(argv[1]);
    double epsilon = stod(argv[2]);
    int min_puntos = stoi(argv[3]);
    int num_hilos = stoi(argv[4]);

    if (num_hilos < 1) {
        cout << "num_hilos debe ser al menos 1\n";
        return 1;
    }

    omp_set_num_threads(num_hilos);

    int k = stoi(argv[5]);


    //validar k ANTES de leer el CSV: si k es inválido, el programa falla de inmediato y no hay nada que liberar.
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

    // CAMBIO: antes era "while (filas * columnas != k)". Si k no era potencia de 2 (por ejemplo 3, 6 o 0),
    // el producto nunca llegaba a ser igual a k y el programa se quedaba en un ciclo infinito.
    // Con "<" el ciclo siempre termina; después revisamos si se llegó exactamente a k.
    while (filas * columnas < k) {

        if (p % 2 == 0) {
            columnas *= 2;
        }
        else {
            filas *= 2;
        }

        p++;
    }

    // para k invalida
    if (filas * columnas != k) {
        cout << "k debe ser una potencia de 2 (1, 2, 4, 8, 16, 32, 64, ...)\n";
        return 1;
    }


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
        cout << "Error al leer el archivo\n";
        return 1;
    }

//Para medir tiempo

    //
    //  el cronómetro ahora empieza ANTES de calcular los límites y construir las particiones
    // (en tu borrador empezaba después). Motivo: ese trabajo solo lo necesita esta versión; el serial
    // no lo hace. Si no se cuenta, el speedup contra el serial sale inflado.
    // Lo único que queda fuera es leer y guardar archivos, igual que en el serial.
    //
    // Más abajo se imprimen tres tiempos:
    //   TIEMPO             = todo (particiones + DBSCAN). Este es el que se compara contra el serial.
    //   TIEMPO_PARTICIONES = solo límites + construcción de bloques (código secuencial, un solo hilo)
    //   TIEMPO_DBSCAN      = solo la región paralela

    double tiempo_inicio = omp_get_wtime();

    // Para particiones espaciales:
    // Obtenemos los límites globales de todos los puntos:
    //
    // limites[0] = min_x
    // limites[1] = max_x
    // limites[2] = min_y
    // limites[3] = max_y
    double* limites = limit_build(puntos, n);
    
    // Ancho de cada bloque sobre el eje x
    double long_part_x = (limites[1] - limites[0]) / columnas;

    // Altura de cada bloque sobre el eje y
    double long_part_y = (limites[3] - limites[2]) / filas;

    // NUEVO: caso raro. Si todos los puntos tienen la misma x (o la misma y), el ancho (o la altura)
    // sería 0 y más abajo dividiríamos entre 0. Con 1, todos los puntos caen en la columna (o fila) 0.
    if(long_part_x == 0){
        long_part_x = 1;
    }

    if(long_part_y == 0){
        long_part_y = 1;
    }

    // Contruir bloques: construir_particiones:

    // 1. Divide el espacio entre min_x, max_x, min_y y max_y.
    // 2. Crea una matriz bloques[fila][columna].
    // 3. Determina a qué bloque pertenece cada punto.
    // 4. Guarda en cada bloque los índices de sus puntos.

    // los bloques NO guardan copias de los puntos. Guardan índices que hacen referencia al arreglo original "puntos".

    Bloque** bloques =
        construir_particiones(
            puntos,
            n,
            filas,
            columnas,
            long_part_x,
            long_part_y,
            limites
        );

    double tiempo_particiones = omp_get_wtime();

    double epsilon_cuadrado = epsilon * epsilon;

    // NUEVO: margen con el que se forma el halo de cada bloque (ver reunir_candidatos).
    // Es epsilon con un 0.1% extra de seguridad: los bordes de los bloques se calculan con números
    // decimales (double), que pueden tener errores de redondeo diminutos, y con este colchón un vecino
    // real nunca se queda fuera del halo por culpa de eso.
    // No afecta el resultado: lo peor que puede pasar es que entren al halo unos pocos puntos de más,
    // y la distancia exacta se vuelve a comprobar después, al contar vecinos.
    
    double margen = epsilon * 1.001;

    //Aqui empieza lo bueno ahora si:

    // Variables COMPARTIDAS (declaradas antes de la región: todos los hilos ven las mismas).
    // Solo se LEEN, con una excepción: el campo asignacion de los puntos, que se escribe
    // únicamente con marcar_en_cluster (operaciones atómicas):

    //     puntos, bloques, n, k, columnas, epsilon_cuadrado, margen, min_puntos


    // Variables PRIVADAS (declaradas dentro de la región o del ciclo: cada hilo tiene su propia copia):

    //     candidatos, indices_vecinos, b, fila_b, columna_b, cantidad_propios, num_candidatos,
    //     pos, i, xi, yi, num_vecinos, c, j, dx, dy, distancia_cuadrada, v 

    detectar_ruido_parallel_k(puntos, n, bloques, k, filas, columnas, margen, long_part_x, long_part_y, epsilon_cuadrado, min_puntos);


    // Termina la región paralela. Sigue un solo hilo (el principal), que ya ve las marcas de todos los bloques.
    double tiempo_fin = omp_get_wtime();

    cout << "TIEMPO=" << (tiempo_fin - tiempo_inicio) << "\n";
    cout << "TIEMPO_PARTICIONES=" << (tiempo_particiones - tiempo_inicio) << "\n";
    cout << "TIEMPO_DBSCAN=" << (tiempo_fin - tiempo_particiones) << "\n";

    int resultado = guardar_csv(nombreSalida, n, puntos); 

    liberar_particiones(bloques, filas, columnas); 
    delete[] limites;
    delete[] puntos;

    return resultado; // 0 si todo salió bien, 1 si falló al guardar


    // Para correr el archivo:
    // 1. Se compila el script (¡con -fopenmp!):
    //      g++ -O2 -std=c++17 -fopenmp src/parallel_k.cpp -o src/parallel_k
    // 2. Se ejecuta en la terminal, desde la carpeta raíz del proyecto:
    //      ./src/parallel_k n epsilon min_puntos num_hilos k
    //    Ejemplo:  ./src/parallel_k 20000 0.03 10 4 8
*/

// EJECUCIÓN EXPERIMENTAL 

// Ejemplo de ejecución:
    //
    // ./parallel_k 20000 0.03 10 4 8 10
    //
    // argv[0] = "./parallel_k"
    // argv[1] = "20000"   -> número de puntos
    // argv[2] = "0.03"    -> epsilon
    // argv[3] = "10"      -> mínimo de puntos
    // argv[4] = "4"       -> número de hilos
    // argv[5] = "8"       -> número de particiones k
    // argv[6] = "10"       -> repeticiones de la busqueda

    // argc = 7

    if (argc != 7) {
        cout << "Uso: ./parallel_k n epsilon min_puntos num_hilos k num_pruebas\n";
        return 1;
    }

    int n = stoi(argv[1]);
    double epsilon = stod(argv[2]);
    int min_puntos = stoi(argv[3]);
    int num_hilos = stoi(argv[4]);

    if (num_hilos < 1) {
        cout << "num_hilos debe ser al menos 1\n";
        return 1;
    }

    omp_set_num_threads(num_hilos);

    int k = stoi(argv[5]);

    // Cantidad de repeticiones de la detección de ruido para esta configuración.
    int num_pruebas = stoi(argv[6]);
    if(num_pruebas < 1){
        cout << "num_pruebas debe ser al menos 1\n";
        return 1;
    }

    int filas = 1;
    int columnas = 1;

    int p = 0;

    while (filas * columnas < k) {

        if (p % 2 == 0) {
            columnas *= 2;
        }
        else {
            filas *= 2;
        }

        p++;
    }

    // para k invalida
    if (filas * columnas != k) {
        cout << "k debe ser una potencia de 2 (1, 2, 4, 8, 16, 32, 64, ...)\n";
        return 1;
    }


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
        cout << "Error al leer el archivo\n";
        return 1;
    }

    // -------------------------
    // Particiones
    // -------------------------

    double* limites = limit_build(puntos, n);
    
    double long_part_x = (limites[1] - limites[0]) / columnas;

    double long_part_y = (limites[3] - limites[2]) / filas;

    if(long_part_x == 0){
        long_part_x = 1;
    }

    if(long_part_y == 0){
        long_part_y = 1;
    }

    Bloque** bloques =
        construir_particiones(
            puntos,
            n,
            filas,
            columnas,
            long_part_x,
            long_part_y,
            limites
        );

    double epsilon_cuadrado = epsilon * epsilon;
    
    double margen = epsilon * 1.001;

    
    // -------------------------
    // Experimentos
    // -------------------------

    double suma_tiempos = 0;

    for (int prueba=0; prueba < num_pruebas; prueba ++){

        for (int i=0; i<n; i++){
            puntos[i].asignacion = -1;
        }

        double inicio_dbscan = omp_get_wtime();

        detectar_ruido_parallel_k(puntos, n, bloques, k, filas, columnas, margen, long_part_x, long_part_y, epsilon_cuadrado, min_puntos);

        double fin_dbscan = omp_get_wtime();

        suma_tiempos += fin_dbscan - inicio_dbscan;

    }

    cout << "TIEMPO=" << (suma_tiempos/num_pruebas) << "\n";

    int resultado = guardar_csv(nombreSalida, n, puntos); 

    liberar_particiones(bloques, filas, columnas); 
    delete[] limites;
    delete[] puntos;

    return resultado; // 0 si todo salió bien, 1 si falló al guardar


    // Para correr el archivo:
    // 1. Se compila el script (¡con -fopenmp!):
    //      g++ -O2 -std=c++17 -fopenmp src/parallel_k.cpp -o src/parallel_k
    // 2. Se ejecuta en la terminal, desde la carpeta raíz del proyecto:
    //      ./src/parallel_k n epsilon min_puntos num_hilos k
    //    Ejemplo:  ./src/parallel_k 20000 0.03 10 4 8
}
