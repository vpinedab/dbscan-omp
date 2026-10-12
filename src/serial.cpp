#include <fstream>
#include <iostream>
#include <string>

using namespace std;

struct Punto{
    double x;
    double y;
    int asignacion=-1;
};

Punto* leer_csv(string nombreArchivo, int n){
    ifstream archivo(nombreArchivo);// Crea el objeto archivo de tipo ifstream e intenta abrir el archivo cuyo nombre/ruta está en nombreArchivo.

    if(!archivo.is_open()) { // Verifica si la apertura funcionó.
        return nullptr; // nullptr es un puntero nulo de forma segura y tipada
    }

    Punto* puntos = new Punto[n];
    double x,y;
    char coma;

    for (int i=0; i<n; i++){
        archivo >> x >> coma >> y; 
        // >> es un operador de extracción de flujos
        // Se utiliza para leer datos de forma secuencial desde un archivo y guardarlos en variables paso a paso.
        // El operador no se detiene línea por línea, solo busca el siguiente dato que coincida con el tipo de tu variable.
        // Se salta automáticamente los espacios en blanco y saltos de línea

        puntos[i].x = x;
        puntos[i].y = y;
    }

    archivo.close();

    return puntos;

}

int guardar_csv(string nombreArchivo, int n, Punto* puntos){
    
    ofstream archivo(nombreArchivo); // Crea el archivo automáticamente si este no existe en la ruta que le indicaste.

    if(!archivo.is_open()){
        cout << "Error al crear el archivo de salida \n"; // Imprimer el mensaje
        return 1;
    }

    for (int i=0; i<n;i++){
        archivo << puntos[i].x << "," << puntos[i].y << "," << puntos[i].asignacion << "\n";
    }

    archivo.close();

    return 0;

}

int main(int argc, char** argv) { 
    // En c++, main no puede recibir los parámetros de forma directa
    // El sistema le pasa argumentos al programa como texto 
    // Donde argc es la cantidad de argumentos, argv es el arreglo de argumentos, todos como texto

    // Ejemplo: 
    // 
    // ./serial 20000 0.03 10
    //
    // argv[0] = "./serial"
    // argv[1] = "20000"
    // argv[2] = "0.03"
    // argv[3] = "10"
    // 
    // argc = 4

    if (argc != 4) {
        cout << "Uso: ./serial n epsilon min_puntos\n";
        return 1;
    }

    int n = stoi(argv[1]); // Convierte una cadena de texto en un número entero de tipo int
    double epsilon = stod(argv[2]);
    int min_puntos = stoi(argv[3]);

    string nombreEntrada = "data/input/" + to_string(n) + "_data.csv"; // Se debe correr desde la carpeta donde está el proyecto
    string nombreSalida = "data/output/" + to_string(n) + "_results.csv";
    Punto * puntos = leer_csv(nombreEntrada, n);

    if (puntos == nullptr) {
        cout << "Error al leer el archivo\n";
        return 1;
    }

    int* indices_vecinos = new int[n];
    int num_vecinos=0;
    double distancia_cuadrada=0;
    double epsilon_cuadrado=epsilon*epsilon;

    for(int i=0; i<n;i++){

        num_vecinos=0;

        for(int j=0; j<n; j++){

            if(i!=j){

                distancia_cuadrada = (puntos[j].x-puntos[i].x)*(puntos[j].x-puntos[i].x)
                            + (puntos[j].y-puntos[i].y)*(puntos[j].y-puntos[i].y);

                if(distancia_cuadrada <= epsilon_cuadrado){
                    indices_vecinos[num_vecinos]=j;
                    num_vecinos++;
                }
            }

        }

        if(num_vecinos+1>=min_puntos){
            
            puntos[i].asignacion=1;

            for(int j=0; j<num_vecinos;j++){

                puntos[indices_vecinos[j]].asignacion=1;

            }

        }
    }

    int resultado = guardar_csv(nombreSalida, n, puntos);

    delete[] indices_vecinos;
    delete[] puntos;

    return resultado;

    // Para correr el archivo: 
    // 1. Se compila el script
    // 2. Se ejecuta en la terminal: ./src/serial n epsilon min_puntos

}