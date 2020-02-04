#include "filezilla.h"
#include "main.h"

#include "xml.h"
#include "base64.h"
#include "functions.h"

int parse_sitemanager_xml(int verbose, const char *output_file, const char *master_password, char *path) {
	xmlDocPtr doc;
	xmlNodePtr cur;

	if(init_xml_file(&doc, &cur, path) == -1) {
		fprintf(stderr, "init_xml_file() failure\n");
		return -1;
	}

	cur = cur->xmlChildrenNode;
	cur = retrieve_xml_node(cur, "Servers");
	
	cur = cur->xmlChildrenNode;
	char *node_name = "Server";
	while(cur != NULL) {
		if(xmlStrcmp(cur->name, (const xmlChar *) node_name) == 0) {
			parse_xml_password(doc, cur->xmlChildrenNode);	
		}
		cur = cur->next;
	}

	xmlFreeDoc(doc);
	xmlCleanupParser();

	return 1;
}

int parse_recentservers_xml(int verbose, const char *output_file, const char *master_password, char *path) {
	xmlDocPtr doc;
	xmlNodePtr cur;

	if(init_xml_file(&doc, &cur, path) == -1) {
		fprintf(stderr, "init_xml_file() failure\n");
		return -1;
	}

	cur = cur->xmlChildrenNode;
	cur = retrieve_xml_node(cur, "RecentServers");
	
	cur = cur->xmlChildrenNode;
	char *node_name = "Server";
	while(cur != NULL) {
		if(xmlStrcmp(cur->name, (const xmlChar *) node_name) == 0) {
			parse_xml_password(doc, cur->xmlChildrenNode);	
		}
		cur = cur->next;
	}

	xmlFreeDoc(doc);
	xmlCleanupParser();

	return 1;
}

int parse_xml_password(xmlDocPtr doc, xmlNodePtr cur) {
	xmlChar *key;
	char *host = NULL;
	char *username = NULL;
	char *cipher_password = NULL;
	char *plaintext_password = NULL;
	char port[6] = {-1};

	while(cur != NULL) {
		if (cur->type == XML_ELEMENT_NODE) {
			key = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			if(strcmp(cur->name, "User") == 0) {
				username = malloc(strlen(key)+1);
				safe_strcpy(username, key, strlen(key));
			}
			else if(strcmp(cur->name, "Pass") == 0) {
				cipher_password = malloc(strlen(key)+1);
				safe_strcpy(cipher_password, key, strlen(key));
			}
			else if(strcmp(cur->name, "Host") == 0) {
				host = malloc(strlen(key)+1);
				safe_strcpy(host, key, strlen(key));
			}
			else if(strcmp(cur->name, "Port") == 0) {
				safe_strcpy(port, key, strlen(key));
			}
		}
		cur = cur->next;
	}

	if(cipher_password != NULL) {
		int len_cipher_password = strlen(cipher_password);
		if(base64_decode(cipher_password, &plaintext_password, &len_cipher_password) == -1) {
			free(username);
			free(cipher_password);
			free(host);
			fprintf(stderr, "base64_decode() failure\n");
			return -1;
		}
		free(cipher_password);
	}

	printf("\n");
	if(host != NULL) {
		printf("[+] Host: %s\n", host);
		free(host);
	}
	if(port[0] != -1) {
		printf("[+] Port: %s\n", port);
	}
	if(username != NULL) {
		printf("[+] Username: %s\n", username);
		free(username);
	}
	if(plaintext_password != NULL) {
		printf("[+] Password : %s\n", plaintext_password);
		free(plaintext_password);
	}
	printf("\n");
}

int dump_filezilla(int verbose, const char *output_file, const char *master_password) {
	puts("[*] Starting FileZilla dump ...");

	char filezilla_sitemanager_path[MAX_PATH_SIZE];
	char filezilla_recentservers_path[MAX_PATH_SIZE];

	load_filezilla_paths(filezilla_sitemanager_path, filezilla_recentservers_path);

	int result = 0;

	if(access(filezilla_sitemanager_path, 0) != -1) {
		result = parse_sitemanager_xml(verbose, output_file, master_password, filezilla_sitemanager_path);
	}

	if(access(filezilla_sitemanager_path, 0) != -1) {
		result = parse_recentservers_xml(verbose, output_file, master_password, filezilla_recentservers_path);
	}

	if(result == 0) {
		fprintf(stderr, "[-] Couldn't find any FileZilla installation\n");
		return -1;
	}
	else if (result == -1) {
		fprintf(stderr, "[-] An error occured\n");
		return -1;
	}

	return 1;
}