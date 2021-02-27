#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include "threadpool.h"
#define PROTOCOL1 "HTTP/1.0"
#define PROTOCOL2 "HTTP/1.1"

#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
char *get_mime_type(char *name);
void makeDir(char *folder_name, int fd, struct stat tmp);
void request_failed(char *type, int socket_fd, char *protocol, char *path);
void return_file(char *fileName, int fd, struct stat tmp);
char *current_time() //return current time
{
	time_t now;
	char *timebuf = (char *)calloc(128, 1);
	char temp[128];
	temp[0] = 0;
	now = time(NULL);
	strftime(temp, sizeof(temp), RFC1123FMT, gmtime(&now));
	for (int i = 0; i < strlen(temp); i++)
	{
		timebuf[i] = temp[i];
	}
	return timebuf;
}

void error(char *msg) //return error msg
{
	perror(msg);
	printf("\nerror:: %s \n", msg);
	exit(1);
}

int mission(char *argv) //read request and act
{
	int fd = atoi(argv);
	char buffer[4000];
	buffer[0] = 0;
	int rc = read(fd, buffer, 4000);
	if (rc < 0) //read failed.
		error("read() failed.\n");
	if (rc == 0)
	{
		return 0;
	}
	int args_counter = 1;
	int i = 0;
	while (i < rc)
	{
		if (buffer[i] == ' ')
		{
			args_counter++;
		}
		i++;
	}
	char *end_line = strstr(buffer, "\r\n");
	*end_line = '\0';
	char *request_type = strtok(buffer, " ");
	char *path = strtok(NULL, " ");
	char *protocol = strtok(NULL, "\0");
	printf("request type: %s\n", request_type);
	printf("path: %s\n", path);
	printf("protocol: %s\n", protocol);
	if (!path || !protocol || !request_type)
	{
		request_failed("400", fd, protocol, path);
		close(fd);
		return -1;
		//400 bad request
	}

	if ((strcmp(protocol, PROTOCOL1) != 0 && strcmp(protocol, PROTOCOL2) != 0))
	{
		//protocol is not HTTP 1.0 OR HTTP 1.1
		request_failed("400", fd, protocol, path);
		close(fd);
		return -1;
		//400 bad request
	}
	if (strcmp(request_type, "GET") != 0)
	{
		//request is not GET
		request_failed("501", fd, protocol, path);
		close(fd);
		return -1;
		//501 not supported
	}
	char *path_for_access = (char *)calloc(strlen(path) + 2, 1);
	path_for_access[0] = '.';
	strcat(path_for_access, path);
	printf("\n%s\n", path_for_access);

	if (access(path_for_access, F_OK) != 0) // the requested path invalid
	{
		request_failed("404", fd, protocol, path);
		printf("\n\n closing fd... \n\n");
		close(fd);
		free(path_for_access);
		return -1;
		// 404 Not Found
	}
	else
	{
		int counter = 0;
		char *dir = (char *)calloc(strlen(path_for_access) + 1, 1);
		while (counter < strlen(path_for_access))
		{
			dir[counter] = path_for_access[counter];
			if (path_for_access[counter] == '/' && counter == strlen(path_for_access) - 1)
			{
				struct stat tmp;
				printf("\ndir: %s \n", dir);
				if (stat(dir, &tmp) == -1)
				{
					// server error
					request_failed("501", fd, protocol, path);
				}
				//check premissions for this FOLDER
				if (!(S_IXOTH & tmp.st_mode))
				{
					// forbidden
					request_failed("403", fd, protocol, path);
				}
				else
				{
					printf("\n\n	**	ACCESS IS OK	1	**	\n\n");
					char *index_path = (char *)calloc(sizeof("/index.html") + strlen(path_for_access) + 1, 1);
					strcat(index_path, path_for_access);
					strcat(index_path, "/index.html");
					if (access(index_path, F_OK) != 0)
					{
						//display folder content
						printf("\nNO INDEX FILE IN THIS FOLDER\n");
						//NO INDEX FILE
						//MAKE DIR
						makeDir(path_for_access, fd, tmp);
					}
					else
					{
						printf("\nuser ask for folder, index.html exist, return it. index path:  %s \n", index_path);
						return_file(index_path, fd, tmp);
					}
					free(index_path);
				}
			}
			else if (path_for_access[counter] != '/' && counter == strlen(path_for_access) - 1) //
			{
				struct stat tmp;
				printf("\ndir: %s \n", dir);
				if (stat(dir, &tmp) == -1)
				{
					// server error
					request_failed("501", fd, protocol, path);
				}
				if (S_ISDIR(tmp.st_mode))
				{
					//check premissions for this FOLDER
					if (!(S_IXOTH & tmp.st_mode))
					{
						//forbidden
						request_failed("403", fd, protocol, path);
					}
					else
					{
						printf("\n\n	**	PATH IS DIR BUT NOT END WITH '/'	**	\n\n");
						request_failed("302", fd, protocol, path);
						close(fd);
						free(path_for_access);

						return -1;
					}
				}
				else if (S_ISREG(tmp.st_mode))
				{
					//client requesting a FILE.
					if (!(S_IROTH & tmp.st_mode))
					{
						//forbidden
						request_failed("403", fd, protocol, path);
						close(fd);
						free(path_for_access);

						return -1;
					}
					else
					{
						printf("\n\n	**	ACCESS IS OK	3	**	\n\n");
						// Accesss to file granted, return file to client.
						return_file(path_for_access, fd, tmp);
					}
				}
				else
				{
					//forbidden
					request_failed("403", fd, protocol, path);
					close(fd);
					free(path_for_access);

					return -1;
				}
			}
			counter++;
		}
		free(dir);
	}
	printf("MISSION ENDED\n");
	free(path_for_access);
	close(fd);
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc != 4)
	{
		printf("Command line usage: server <port> <pool-size> <max-number-of-request>\n");
		exit(EXIT_FAILURE);
	}
	int pool_size = atoi(argv[2]); //pool size initillazion
	threadpool *pool = create_threadpool(pool_size);
	if (pool == NULL)
	{
		error("pool create failed\n");
	}
	int port = atoi(argv[1]); // port initillazion
	if (pool_size < 1)
	{
		error("pool size invalid\n");
	}
	int max_num_of_request = atoi(argv[3]);
	int welcome_socket;
	struct sockaddr_in server_addr;
	struct sockaddr client_address;
	socklen_t client_len = sizeof(struct sockaddr_in);
	welcome_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (welcome_socket < 0)
	{
		error("main socket init failed\n");
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);
	/*============ BIND ==============*/
	if (bind(welcome_socket, (__CONST_SOCKADDR_ARG)&server_addr, sizeof(server_addr)) < 0)
	{
		error("bind failed\n");
	}
	/*============ LISTEN ==============*/
	if (listen(welcome_socket, 5) < 0)
	{
		error("listen failed\n");
	}
	int *sockets = calloc(sizeof(int) * max_num_of_request, sizeof(int));
	//get request:
	for (int i = 0; i < max_num_of_request; i++)
	{
		printf("\nMain LOOP %d STTART\n", i);
		sockets[i] = accept(welcome_socket, &client_address, &client_len);
		if (sockets[i] < 0)
		{
			error("Socket accept failed");
		}
		char private_socket_buffer[256];
		sprintf(private_socket_buffer, "%d", sockets[i]);
		dispatch(pool, (dispatch_fn)mission, private_socket_buffer);
		printf("\nMain LOOP %d  END\n", i);
	}
	close(welcome_socket);
	destroy_threadpool(pool);
	free(sockets);
	//free(pool);
	return 0;
}
void return_file(char *fileName, int fd, struct stat tmp)
{
	printf("\n\n	**	RETURNING FILE . . .	**	\n\n");
	// Accesss to file granted, return file to client.
	char respond[4000];
	respond[0] = 0;
	strcat(respond, PROTOCOL2);
	strcat(respond, " 200 OK\r\n");
	strcat(respond, "Server: webserver/1.0\r\n");
	strcat(respond, "Date: ");
	char *timebuf = current_time();
	strcat(respond, timebuf);
	strcat(respond, "\r\n");
	strcat(respond, "Content-Type: ");
	char *content_type = get_mime_type(fileName);
	printf("\n* dir type: %s *\n", fileName);
	if (content_type == NULL)
		content_type = "";
	printf("\n* content type: %s *\n", content_type);
	strcat(respond, content_type);
	strcat(respond, "\r\n");
	strcat(respond, "Content-Length: ");
	char len[5];
	len[0] = 0;
	sprintf(len, "%ld", tmp.st_size);
	printf("\n	bufiiiif	\n");
	printf("\n	%s len	\n", len);
	strcat(respond, len);
	strcat(respond, "\r\n");
	strcat(respond, "Last-Modified: ");
	char timebuf_last_mod[128];
	strftime(timebuf_last_mod, sizeof(timebuf_last_mod), RFC1123FMT, gmtime(&tmp.st_mtime));
	strcat(respond, timebuf_last_mod);
	strcat(respond, "\r\n");
	strcat(respond, "Connection: close\r\n\r\n");
	printf("\n	RESPOND:\n%s	\n", respond);

	if (write(fd, respond, strlen(respond)) < 0)
	{
		error("write failed");
	}

	FILE *sourceFile;
	sourceFile = fopen(fileName, "r");

	if (sourceFile == NULL)
	{
		printf("\n	open file failed	\n");
		exit(EXIT_FAILURE);
	}
	unsigned char buf[1000000];
	buf[0] = 0;
	int readit = 0;
	readit = fread(buf, 1, 1000000, sourceFile);
	if (write(fd, buf, readit) < 0)
	{
		error("write failed");
	}
	close(fd);
	fclose(sourceFile);
	free(timebuf);
}
void makeDir(char *folder_name, int fd, struct stat tmp)
{
	printf("\nMAKING DIR . . .\n");
	char respond[4000];
	respond[0] = 0;
	strcat(respond, PROTOCOL2);
	strcat(respond, " 200 OK\r\n");
	strcat(respond, "Server: webserver/1.0\r\n");
	strcat(respond, "Date: ");
	char *timebuf = current_time();
	strcat(respond, timebuf);
	strcat(respond, "\r\n");
	strcat(respond, "Content-Type: ");
	strcat(respond, "text/html");
	strcat(respond, "\r\n");
	strcat(respond, "Content-Length: ");
	char len[5];
	sprintf(len, "%ld", tmp.st_size);
	printf("\n	%s len	\n", len);
	strcat(respond, len);
	strcat(respond, "\r\n");
	strcat(respond, "Last-Modified: ");
	char timebuf_last_mod[128];
	strftime(timebuf_last_mod, sizeof(timebuf_last_mod), RFC1123FMT, gmtime(&tmp.st_mtime));
	strcat(respond, timebuf_last_mod);
	strcat(respond, "\r\n");
	strcat(respond, "Connection: close\r\n\r\n");
	printf("\n	RESPOND:\n%s	\n", respond);
	if (write(fd, respond, strlen(respond)) < 0)
	{
		error("write failed");
	}
	else
	{
		printf("\n	1'ST RESPOND SENT SUCCESFULY	\n");
	}
	free(timebuf);
	char respond_html[4000];
	respond_html[0] = 0;
	strcat(respond_html, "<HTML><HEAD><TITLE>Index of ");
	strcat(respond_html, folder_name);
	strcat(respond_html, "</TITLE></HEAD><BODY><H4>Index of ");
	strcat(respond_html, folder_name);
	strcat(respond_html, "</H4><table CELLSPACING=8><tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>");
	//FILES details will be build here file by file in rows.
	DIR *dir = opendir(folder_name);
	if (dir == NULL)
	{
		error("\nDIR open failed.\n");
	}

	//Now scan folder:
	struct dirent *dentry;
	while ((dentry = readdir(dir)) != NULL)
	{
		//printf("%s\n", dentry->d_name);
		strcat(respond_html, "<tr>");
		strcat(respond_html, "<td><A HREF=\"");
		strcat(respond_html, dentry->d_name);
		strcat(respond_html, "\">");
		strcat(respond_html, dentry->d_name);
		strcat(respond_html, "</A></td><td>");
		/* STAT for each file */
		char *last_mod_time = (char *)calloc(1, 128);
		char *path_for_each_file = NULL;
		path_for_each_file = (char *)calloc(1, 512);
		strcat(path_for_each_file, folder_name);	// creeate path for each file
		strcat(path_for_each_file, dentry->d_name); // add file name
		//path_for_each_file++;
		struct stat tmp1;
		printf("\nfile : %s \n", path_for_each_file);
		if (stat(path_for_each_file, &tmp1) == -1)
		{
			// server error
			printf("\n faild to stat file : %s\n", path_for_each_file);
			request_failed("501", fd, PROTOCOL2, path_for_each_file);
			close(fd);
		}
		if (S_ISDIR(tmp1.st_mode))
		{
			//check premissions for this FOLDER
			if (!(S_IXOTH & tmp1.st_mode))
			{
				//forbidden
				//request_failed("403", fd, protocol, path);
			}
			else
			{
				strftime(last_mod_time, sizeof(last_mod_time), RFC1123FMT, gmtime(&tmp1.st_mtime));
				strcat(respond_html, last_mod_time);
				strcat(respond_html, "</td><td>");
			}
		}
		else if (S_ISREG(tmp1.st_mode))
		{
			//client requesting a FILE.
			if (!(S_IROTH & tmp1.st_mode))
			{
				//forbidden
				//request_failed("403", fd, PROTOCOL2, path_for_each_file);
			}
			else
			{
				printf("\n\n	**	Table row OK	**	\n\n");
				// Accesss to file granted, return file data to table.
				strftime(last_mod_time, sizeof(last_mod_time), RFC1123FMT, gmtime(&tmp1.st_mtime));
				strcat(respond_html, last_mod_time);
				strcat(respond_html, "</td><td>");
				char size_str[128];
				sprintf(size_str, "%ld", tmp1.st_size);
				strcat(respond_html, size_str); //add file size
			}
		}

		strcat(respond_html, "</td></tr>"); //end item:
		free(last_mod_time);
		free(path_for_each_file);
	}
	strcat(respond_html, "</table><HR><ADDRESS>webserver/1.0</ADDRESS></BODY></HTML>");
	if (write(fd, respond_html, strlen(respond_html)) < 0)
	{
		error("write failed");
	}

	if (closedir(dir) == -1)
	{
		error("\nDIR close failed.\n");
	}
	printf("\nMAKEDIR COMPLETE.\n");
}
void request_failed(char *type, int socket_fd, char *protocol, char *path)
{
	if (strcmp(type, "501") == 0) //  501 Method is not GET
	{
		printf("\n\n **501** \n\n");
		char respond[4000];
		respond[0] = 0;
		strcat(respond, PROTOCOL2);
		strcat(respond, " 501 Not supported\r\n");
		strcat(respond, "Server: webserver/1.0\r\n");
		strcat(respond, "Date: ");
		char *timebuf = current_time();
		strcat(respond, timebuf);
		strcat(respond, "\r\n");
		strcat(respond, "Content-Type: ");
		char *content_type = get_mime_type(".html");
		strcat(respond, content_type);
		strcat(respond, "\r\n");
		strcat(respond, "Content-Length: ");
		char *html = "<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD>\r\n<BODY><H4>501 Not supported</H4>\r\nMethod is not supported.\r\n</BODY></HTML>";
		char len[5];
		sprintf(len, "%ld", strlen(html));
		strcat(respond, len);
		strcat(respond, "\r\n");
		strcat(respond, "Connection: close\r\n\r\n");
		strcat(respond, html);
		//printf(respond);
		if (write(socket_fd, respond, strlen(respond)) < 0)
		{
			error("write failed");
		}
		free(timebuf);
	}
	if (strcmp(type, "400") == 0) // 400 Bad request.
	{
		char respond[300];
		respond[0] = 0;
		printf("\n\n **400** \n\n");
		strcat(respond, PROTOCOL2);
		strcat(respond, " 400 Bad Request\r\n");
		strcat(respond, "Server: webserver/1.0\r\n");
		strcat(respond, "Date: ");
		char *timebuf = current_time();
		strcat(respond, timebuf);
		strcat(respond, "\r\n");
		strcat(respond, "Content-Type: ");
		char *content_type = get_mime_type(".html");
		strcat(respond, content_type);
		strcat(respond, "\r\n");
		strcat(respond, "Content-Length: ");
		char *html = "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD><BODY><H4>400 Bad request</H4>Bad Request.</BODY></HTML>";
		char len[5];
		sprintf(len, "%ld", strlen(html));
		strcat(respond, len);
		strcat(respond, "\r\n");
		strcat(respond, "Connection: close\r\n\r\n");
		strcat(respond, html);
		//printf(respond);
		if (write(socket_fd, respond, strlen(respond)) < 0)
		{
			error("write failed");
		}
		free(timebuf);
	}
	if (strcmp(type, "404") == 0) // 404 requested path does not exist.
	{
		char respond[4000];
		respond[0] = 0;
		printf("\n\n **404** \n\n");
		strcat(respond, PROTOCOL2);
		strcat(respond, " 404 Not Found\r\n");
		strcat(respond, "Server: webserver/1.0\r\n");
		strcat(respond, "Date: ");
		char *timebuf = current_time();
		strcat(respond, timebuf);
		strcat(respond, "\r\n");
		strcat(respond, "Content-Type: ");
		char *content_type = get_mime_type(".html");
		strcat(respond, content_type);
		strcat(respond, "\r\n");
		strcat(respond, "Content-Length: ");
		char *html = "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\r\n<BODY><H4>404 Not Found</H4>\r\nFile not found.\r\n</BODY></HTML>";
		char len[5];
		sprintf(len, "%ld", strlen(html));
		strcat(respond, len);
		strcat(respond, "\r\n");
		strcat(respond, "Connection: close\r\n\r\n");
		strcat(respond, html);
		if (write(socket_fd, respond, strlen(respond)) < 0)
		{
			error("write failed");
		}
		free(timebuf);
	}
	if (strcmp(type, "302") == 0) // 302 path is directory but it does not end with a '/'.
	{
		char respond[4000];
		printf("\n\n **302** \n\n");
		strcat(respond, PROTOCOL2);
		strcat(respond, " 302 Found\r\n");
		strcat(respond, "Server: webserver/1.0\r\n");
		strcat(respond, "Date: ");
		char *timebuf = current_time();
		strcat(respond, timebuf);
		strcat(respond, "\r\n");
		strcat(respond, "Location: ");
		strcat(respond, path);
		strcat(respond, "/\r\n");
		strcat(respond, "Content-Type: ");
		char *content_type = get_mime_type(".html");
		strcat(respond, content_type);
		strcat(respond, "\r\n");
		strcat(respond, "Content-Length: ");
		char *html = "<HTML><HEAD><TITLE>302 Found</TITLE></HEAD>\r\n<BODY><H4>302 Found</H4>\r\nDirectories must end with a slash.\r\n</BODY></HTML>";
		char len[5];
		sprintf(len, "%ld", strlen(html));
		strcat(respond, len);
		strcat(respond, "\r\n");
		strcat(respond, "Connection: close\r\n\r\n");
		strcat(respond, html);
		if (write(socket_fd, respond, strlen(respond)) < 0)
		{
			error("write failed");
		}
		free(timebuf);
	}
	if (strcmp(type, "403") == 0) // 404 requested path does not exist.
	{
		char respond[4000];
		printf("\n\n **403** \n\n");
		strcat(respond, PROTOCOL2);
		strcat(respond, " 403 Forbidden\r\n");
		strcat(respond, "Server: webserver/1.0\r\n");
		strcat(respond, "Date: ");
		char *timebuf = current_time();
		strcat(respond, timebuf);
		strcat(respond, "\r\n");
		strcat(respond, "Content-Type: ");
		char *content_type = get_mime_type(".html");
		strcat(respond, content_type);
		strcat(respond, "\r\n");
		strcat(respond, "Content-Length: ");
		char *html = "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\r\n<BODY><H4>403 Forbidden</H4>\r\nAccess denied.\r\n</BODY></HTML>";
		char len[5];
		sprintf(len, "%ld", strlen(html));
		strcat(respond, len);
		strcat(respond, "\r\n");
		strcat(respond, "Connection: close\r\n\r\n");
		strcat(respond, html);
		if (write(socket_fd, respond, strlen(respond)) < 0)
		{
			error("write failed");
		}
		free(timebuf);
	}
}
char *get_mime_type(char *name)
{
	char *ext = strrchr(name, '.');
	if (!ext)
		return NULL;
	if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
		return "text/html";
	if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(ext, ".gif") == 0)
		return "image/gif";
	if (strcmp(ext, ".png") == 0)
		return "image/png";
	if (strcmp(ext, ".css") == 0)
		return "text/css";
	if (strcmp(ext, ".au") == 0)
		return "audio/basic";
	if (strcmp(ext, ".wav") == 0)
		return "audio/wav";
	if (strcmp(ext, ".avi") == 0)
		return "video/x-msvideo";
	if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0)
		return "video/mpeg";
	if (strcmp(ext, ".mp3") == 0)
		return "audio/mpeg";
	return NULL;
}
