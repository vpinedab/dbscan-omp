
//  Compilar:  g++ -O2 -std=c++17 -fopenmp src/parallel_full.cpp -o src/parallel_full
//             (-fopenmp es obligatorio: sin esa bandera el compilador ignora los
//              "#pragma omp" y además falla al enlazar las funciones omp_...)
//  Ejecutar:  ./src/parallel_full n epsilon min_puntos [num_hilos]
//             (desde la carpeta raíz del proyecto, igual que el serial)

//Notas:

//Estamos paralelizando el trabajo: el input que se recibe es el mismo, pero se divide el trabajo
//Dividir la informacion es en el parallel_k

//Cada hilo va a procesar un punto i contra todos los puntos j

//Estamos paralelizando el cinclo exterior (el de i) porque depende solo de x y y, que no cambian,
// y no dependen de lo que esten haciendo otros hilos

//Lo unico que se escribe es .asignacion; esto no nos crea condicion de carrera porque solamente
// puede ir de -1 a 1, nunca al reves. No importa el orden de las iteraciones, el -1 se queda en -1,
// los 1 se hacen 1 siempre.

//Cada hilo va a tener su propia lista temporal de vecinos y sus propias variables temporales.

//Marcar un punto como "pertenece a un cluster" se hace con operaciones atomicas, porque dos 
// hilos pueden marcar el mismo punto a la vez.

//Ya mide tiempo

//Codigo:

#include <fstream>
#include <iostream>
#include <string>
#include <omp.h>

using namespace std; // Para escribir cout, string, etc. sin el prefijo "std::"

struct Punto{
    double x;
    double y;
    int asignacion=-1;

    //Como x y y nunca cambian, todos los hilos pueden leerlos al mismo tiempo sin problema. 
    // Lo unico que se ESCRIBE mientras se calcula es asignacion, asi que es lo unico que hay que proteger.
};

// Leer CSV

Punto* leer_csv(string nombreArchivo, int n){
    ifstream archivo(nombreArchivo);

    if(!archivo.is_open()) {
        return nullptr;
    }

    Punto* puntos = new Punto[n];
    double x,y;
    char coma;

    for (int i=0; i<n; i++){
        archivo >> x >> coma >> y;
        puntos[i].x = x;
        puntos[i].y = y;
    }
    archivo.close();

    return puntos;
}

// Guardar CSV

int guardar_csv(string nombreArchivo, int n, Punto* puntos){

    ofstream archivo(nombreArchivo); // data/output/ ya debe existir

    if(!archivo.is_open()){
        cout << "Error al crear archivo de salida";
        return 1;
    }

    for (int i=0; i<n;i++){
        archivo << puntos[i].x << "," << puntos[i].y << "," << puntos[i].asignacion << "\n";
    }

    archivo.close();
    return 0;
}

//Aqui ya cambia del serial:

//Marca el punto indice como "pertenece a un cluster" (asignacion= 1) de forma SEGURA
// cuando varios hilos pueden intentar marcarlo al mismo tiempo.

//Como un mismo punto puede ser vecino de varios puntos nucleo, y esos nucleos
// pueden estar siendo procesados por hilos distintos en este mismo instante, si dos hilos escriben
// en la misma variable a la vez sin proteccion, hay condicion de carrera.

//Tenemos que usar operaciones Atomicas: se ejecutan completa e indivisiblemente. No nos crean un
// cuello de botella como un bloqueo, y como es una operacion basica, conviene mucho usarla.

//Esta funcion la vamos a usar en main:

void marcar_en_cluster(Punto* puntos, int indice){

    int valor_actual; // guardamos aqui lo que leamos de asignacion

    //atomica: copia puntos[indice].asignacion en valor_actual de forma indivisible.
    // #pragma omp atomic read afecta solo a la linea que viene justo despues

    #pragma omp atomic read
    valor_actual = puntos[indice].asignacion;

    // Solo escribimos si el punto todavia no estaba cambiado

    // ¿Por qué no escribir siempre? Porque cada escritura obliga a los demás núcleos del procesador
    // a tirar su copia en caché de esa zona de memoria y volver a traerla. Y en esa misma zona están
    // las x,y de puntos cercanos (un Punto mide 24 bytes y una línea de caché 64, así que caben 2 o 3
    // puntos por línea), que los otros hilos leen sin parar. A eso se le llama "false sharing" y frena
    // a todos los hilos. Revisando primero, cada punto se escribe una sola vez (o casi), en lugar de
    // una vez por cada vecino que lo menciona.
    
    //Nos funciona porque asignacion solo cambia de -1 a 1, si dos hilos ven -1 al mismo tiempo,
    // ambos escriben 1 y el resultado es el mismo que si lo hubiera hecho uno solo.
    if(valor_actual != 1){

        //escribe 1 en puntos[indice].asignacion de forma indivisible.
        #pragma omp atomic write
        puntos[indice].asignacion = 1;
    }
}

// ahora se acepta un 4to argumento OPCIONAL con el numero de hilos que queremos

int main(int argc, char** argv) {
   
    // ./parallel_full 20000 0.03 10 4

    // argv[0]= "./parallel_full"
    // argv[1]= "20000"   -> n
    // argv[2]="0.03"    -> epsilon
    // argv[3]= "10      -> min_puntos
    // argv[4]= "4"       -> num_hilos (opcional)

    // argc=5   (o argc =4 si no se da el numero de hilos)

    //Ahora ademas de aceptar 4 argumentos, aceptamos 5 (porque agregamos num hilos)
    if (argc != 4 && argc != 5) {
        cout << "Uso: ./parallel_full n epsilon min_puntos [num_hilos]";
        return 1;
    }

    int n = stoi(argv[1]);
    double epsilon = stod(argv[2]);
    int min_puntos = stoi(argv[3]);

    if (argc == 5) {
        int num_hilos = stoi(argv[4]);

        if (num_hilos < 1) { // No tiene sentido pedir 0 o negativos
            cout << "num_hilos debe ser al menos 1";
            return 1;
        }

        //si se agrego num hilos, se lo pedimos a OpenMP
        omp_set_num_threads(num_hilos);
    }

    // Si NO se dio num_hilos, OpenMP usa su valor por defecto: la variable de entorno
    // OMP_NUM_THREADS si existe y, si no, todos los núcleos lógicos de la máquina.

    string nombreEntrada = "data/input/" + to_string(n) + "_data.csv"; //correr desde la carpeta donde esta el proyecto
    string nombreSalida = "data/output/parallel_full/" + to_string(n) + "_results_parallel_full.csv";

    Punto * puntos = leer_csv(nombreEntrada, n); // Se lee TODO el archivo en un solo arreglo

    if (puntos == nullptr) {
        cout << "Error al leer el archivo";
        return 1;
    }

    // NUEVO: en el serial, aquí se declaraban indices_vecinos, num_vecinos y distancia_cuadrada.
    // Ya no: si estuvieran aquí, serían UNA sola copia compartida por todos los hilos y se
    // pisarían entre sí. Ahora se declaran dentro de la región paralela (ver más abajo).
    //Osea, cada hilo tiene sus propias variables temp.

    double epsilon_cuadrado=epsilon*epsilon; //solo lectura, los hilos pueden compartirla

    //se inicia el cronómetro. omp_get_wtime() regresa el tiempo "de reloj de pared" en segundos,
    // es decir, el tiempo real que pasa (como un cronómetro en la mano). No usamos clock() porque
    // SUMA el tiempo de CPU de todos los hilos y daría un número mayor que el del serial aunque
    // el programa terminara antes.
    double tiempo_inicio = omp_get_wtime();


    //Ahora si empieza lo bueno:

    //Todo antes de aqui era compartido

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

    delete[] puntos; // Libera la memoria del arreglo de puntos principal

    return resultado; // 0 si todo salió bien, 1 si falló al guardar



    // Para correr el archivo:
    // 1. Se compila el script (¡con -fopenmp!):
    //      g++ -O2 -std=c++17 -fopenmp src/parallel_full.cpp -o src/parallel_full
    // 2. Se ejecuta en la terminal, desde la carpeta raíz del proyecto:
    //      ./src/parallel_full n epsilon min_puntos [num_hilos]
    //    Ejemplo:  ./src/parallel_full 20000 0.03 10 4

}