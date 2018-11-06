# pc-2018b-07
## Ubicación de los materiales
###informe
informe.pdf 
###documentos de soporte para la presentacion
presentacion.pdf
###codigos fuente
####server proxy pop3 y SPCP
server/src/
#### Cliente SPCP
client/src/
####stripmime
stripmime/

##Generación de ejecutables
###Generacion del proxy
```
cd server/src/
make
```
el artefacto generado se encontrara en server/src/
###Generacion del cliente SPCP
```
cd client/src/
make
```
el artefacto generado se encontrara en client/src/
###Generacion del stripmime
```
cd stripmime/
make
```
el artefacto generado se encontrara en stripmime/
##Ejecucion de los artefactos y sus opciones.
###El proxy
para ejecutar el proxy se debe correr como
```
./proxyPop [POSIX STYLE OPTIONS] <origin-server>
```
 Donde las opciones posix disponibles son:

-e filter-error-file      especifica a dónde se dirige la salida de error al ejecutar los filtros, por defecto es /dev/null.

-h                        Imprime el diálogo de ayuda y termina.

-l pop3-address           especifica la dirección en la cual el proxy escuchara, por default son todas las interfaces.

-L config-address         especifica dónde estará escuchando el servicio SPCP, por default escucha en loopback.

-m message                el mensaje que dejara el filtro cuando censura. 

-M censored-media-types   lista de media types que serán censurados

-o management-port        Puerto SCTP donde escuchar ael servicio SPCP, por default es el 9090

-p local-port             puerto donde escuchará el proxy para conexiones TCP entrantes, por default es el 1110

-P origin-port            puerto TCP donde estará escuchando el servidor de origen, por default es el 110

-t cmd                    especifica el comando que se ejecutara para las transformaciones

-v                        imprime la versión del proxy

recordar que el artefacto se encuentra en server/src/
###El cliente
para ejecutar el cliente se debe correr como 
```
./spcpClient [POSIX STYLE OPTIONS] 
```
por defecto asume servidor SPCP en 127.0.0.1:9090. Las opciones posix son:

-L config-address         dirección donde se encuentra el servidor SPCP

-o management-port        el puerto en donde se encuentra escuchando el servidor SPCP

recordar que el artefacto se encuentra en client/src/
###EL stripmime 
para ejecutar el stripmime se corre como
```
./stripmime <optional-input-file>
```
el stripmime utiliza variables de entorno para su funcionamiento, estas son:

FILTER_MEDIAS los media types que filtara el stripmime, en formato csv

FILTER_MSG el mensaje por el cual reemplazara los media types filtrados


en donde se puede especificar el archivo de input o por defecto leera de stdin.

recordar que el artefacto se encuenta en stripmime/
