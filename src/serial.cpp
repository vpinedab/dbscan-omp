#include <fstream>
#include <iostream>
#include <string>
#include <omp.h>

using namespace std; // Permite usar cout, string, ifstream, etc. sin escribir std::

// Estructura que representa cada punto del dataset
struct Punto{
    double x;                // Coordenada x
    double y;                // Coordenada y
    int asignacion = -1;     // -1 = ruido/outlier, 1 = pertenece al cluster
};

// Lee el archivo CSV y regresa un arreglo dinámico de puntos
Punto* leer_csv(string nombreArchivo, int n){

    // Crea un objeto de tipo ifstream e intenta abrir el archivo
    // cuya ruta o nombre viene en nombreArchivo
    ifstream archivo(nombreArchivo);

    // Verifica si el archivo se pudo abrir correctamente
    if(!archivo.is_open()) {
        return nullptr; // nullptr representa un puntero nulo de forma segura
    }

    // Se reserva memoria dinámica para guardar n puntos
    Punto* puntos = new Punto[n];

    double x, y;
    char coma;

    // Se leen exactamente n puntos del archivo
    for (int i = 0; i < n; i++){

        archivo >> x >> coma >> y;

        // >> es un operador de extracción de flujos
        // Sirve para leer datos secuencialmente desde un archivo
        // y guardarlos en variables.

        // El operador >> ignora automáticamente espacios en blanco
        // y saltos de línea cuando corresponde.

        puntos[i].x = x;
        puntos[i].y = y;
    }

    archivo.close();

    // Regresamos el puntero al arreglo de puntos
    return puntos;
}

// Guarda los puntos y su asignación final en un archivo CSV
int guardar_csv(string nombreArchivo, int n, Punto* puntos){

    // Crea el archivo si no existe.
    // La carpeta donde se guarda ya debe existir previamente.
    ofstream archivo(nombreArchivo);

    // Verificamos que el archivo se haya podido crear o abrir
    if(!archivo.is_open()){
        cout << "Error al crear el archivo de salida\n";
        return 1;
    }

    // Escribimos cada punto como:
    //
    // x,y,asignacion

    for (int i = 0; i < n; i++){

        archivo << puntos[i].x << ","
                << puntos[i].y << ","
                << puntos[i].asignacion << "\n";
    }

    archivo.close();

    return 0;
}

int main(int argc, char** argv){

    // En C++, main no recibe directamente parámetros como:
    //
    // main(n, epsilon, min_puntos)
    //
    // El sistema operativo le pasa todos los argumentos como texto.
    //
    // argc = cantidad de argumentos recibidos
    // argv = arreglo que contiene los argumentos como cadenas de texto


    // Ejemplo de ejecución:
    //
    // ./serial 20000 0.03 10
    //
    // argv[0] = "./serial"
    // argv[1] = "20000"   -> n
    // argv[2] = "0.03"    -> epsilon
    // argv[3] = "10"      -> min_puntos
    //
    // argc = 4


    // Verificamos que se hayan recibido los tres parámetros necesarios
    if (argc != 4) {
        cout << "Uso: ./serial n epsilon min_puntos\n";
        return 1;
    }

    // stoi convierte texto a int
    int n = stoi(argv[1]);

    // stod convierte texto a double
    double epsilon = stod(argv[2]);

    int min_puntos = stoi(argv[3]);


    // Construimos las rutas de entrada y salida a partir del número de puntos.

    // El programa debe ejecutarse desde la carpeta raíz del proyecto.

    string nombreEntrada =
        "data/input/" + to_string(n) + "_data.csv";

    string nombreSalida =
        "data/output/serial/" + to_string(n) + "_results.csv";


    // Leemos todos los puntos del CSV y los guardamos en memoria
    Punto* puntos = leer_csv(nombreEntrada, n);

    // Si leer_csv regresa nullptr significa que no pudo abrir el archivo
    if (puntos == nullptr) {
        cout << "Error al leer el archivo\n";
        return 1;
    }


    // indices_vecinos guarda temporalmente los índices de los vecinos
    // encontrados para un punto i.
    //
    // Se reserva espacio para n enteros porque, en el peor caso,
    // un punto podría tener casi todos los demás puntos como vecinos.
    int* indices_vecinos = new int[n];


    // Número de vecinos encontrados para el punto actual
    int num_vecinos = 0;


    // Aquí se guarda temporalmente la distancia entre dos puntos
    double distancia_cuadrada = 0;


    // En lugar de calcular:
    //
    // sqrt(dx^2 + dy^2) <= epsilon
    //
    // usamos:
    //
    // dx^2 + dy^2 <= epsilon^2
    //
    // Así evitamos calcular sqrt(), que es una operación innecesaria
    // para saber si dos puntos están dentro de epsilon.

    double epsilon_cuadrado = epsilon * epsilon;


    // Iniciamos el cronómetro.
    //
    // omp_get_wtime() devuelve tiempo real transcurrido en segundos.
    // Aunque este programa es serial, usamos esta función para que
    // la medición sea comparable con las versiones paralelas.
    //
    // El tiempo medido incluye solamente el algoritmo DBSCAN,
    // no la lectura ni escritura de archivos.

    double tiempo_inicio = omp_get_wtime();


    // Recorremos cada punto del dataset
    for(int i = 0; i < n; i++){

        // Cada punto empieza con cero vecinos encontrados
        num_vecinos = 0;


        // Comparamos el punto i contra todos los demás puntos
        for(int j = 0; j < n; j++){

            // No comparamos un punto consigo mismo
            if(i != j){

                // Distancia euclidiana al cuadrado entre i y j:

                distancia_cuadrada =
                    (puntos[j].x - puntos[i].x) *
                    (puntos[j].x - puntos[i].x)
                    +
                    (puntos[j].y - puntos[i].y) *
                    (puntos[j].y - puntos[i].y);


                // Si la distancia es menor o igual que epsilon,
                // entonces j es vecino de i
                if(distancia_cuadrada <= epsilon_cuadrado){

                    // Guardamos el índice j en el arreglo de vecinos
                    indices_vecinos[num_vecinos] = j;

                    num_vecinos++;
                }
            }
        }


        // DBSCAN considera al propio punto dentro del conteo de min_samples.

        // Si cumple min_puntos, entonces i es un punto core.
        if(num_vecinos + 1 >= min_puntos){

            // Marcamos el propio punto como parte del cluster
            puntos[i].asignacion = 1;


            // También marcamos todos sus vecinos como pertenecientes
            // al cluster.
            
            for(int j = 0; j < num_vecinos; j++){

                puntos[indices_vecinos[j]].asignacion = 1;
            }
        }
    }


    // Detenemos el cronómetro después de terminar el algoritmo
    double tiempo_fin = omp_get_wtime();


    // Imprimimos únicamente el tiempo del algoritmo.
    //
    // Este formato "TIEMPO=" se utiliza después desde Python
    // para recuperar automáticamente el tiempo de ejecución.
    cout << "TIEMPO="
         << (tiempo_fin - tiempo_inicio)
         << "\n";


    // Guardamos el resultado final en CSV
    int resultado =
        guardar_csv(nombreSalida, n, puntos);


    // Liberamos la memoria dinámica que reservamos con new[]
    delete[] indices_vecinos;
    delete[] puntos;


    // Regresamos el resultado de guardar_csv:
    //
    // 0 = todo salió correctamente
    // 1 = ocurrió un error al guardar
    return resultado;


    // Para correr el archivo:
    //
    // 1. Compilar:
    //
    // g++ -O2 -std=c++17 -fopenmp src/serial.cpp -o src/serial
    //
    // 2. Ejecutar desde la raíz del proyecto:
    //
    // ./src/serial n epsilon min_puntos
    //
    // Ejemplo:
    //
    // ./src/serial 20000 0.03 10
}