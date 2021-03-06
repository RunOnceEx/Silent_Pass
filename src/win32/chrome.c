#include "chrome_win.h"
#include "chrome.h"
#include "main.h"

#include "json.h"
#include "base64.h"
#include "functions.h"
#include "log.h"

/**
 * DPAPI decrypt with any password (user password)
 *
 * @return 1 on success, -1 on failure 
 */
int dpapi_decrypt(char *cipher_password, int len_cipher_password, char **plaintext_password) {
	DATA_BLOB encrypted_blob, decrypted_blob;
	encrypted_blob.cbData = len_cipher_password;
	encrypted_blob.pbData = cipher_password; 

	if(!CryptUnprotectData(&encrypted_blob, NULL, NULL, NULL, NULL, 0, &decrypted_blob)) {
		log_error("CryptUnprotectData() failure");
		return -1;
	}
	*plaintext_password = malloc(decrypted_blob.cbData + 1);
	if(*plaintext_password == 0) {
		log_error("malloc() failure");
		free(*plaintext_password);
		return -1;
	}
	memcpy(*plaintext_password, decrypted_blob.pbData, decrypted_blob.cbData);
	(*plaintext_password)[decrypted_blob.cbData] = '\0';

	return 1;
}

/**
 * AEAD decrypting function (with no IV len)
 *
 * @return 1 on success, -1 on failure 
 */
int aead_decrypt(char *cipher_password, int len_cipher_password, char *key, char *iv, int len_iv, char **plaintext_password) {
	EVP_CIPHER_CTX *ctx;
	int len;
	int plaintext_len;
    (void) len_iv;

	// The tag is appended at the end of the cipher data
	int tag_offset = len_cipher_password-16;

	// Cipher_password len always greater or equal to plaintext
	*plaintext_password = (unsigned char *)malloc(len_cipher_password);
	if(*plaintext_password == 0) {
		log_error("malloc() failure");
		free(*plaintext_password);
		return -1;
	}

	if(!(ctx = EVP_CIPHER_CTX_new())) {
		log_error("EVP_CIPHER_CTX_new() failure");
		ERR_print_errors_fp(stderr);
		return -1;
	}

	if(!EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) {
		log_error("EVP_DecryptInit_ex() failure");
		ERR_print_errors_fp(stderr);
		return -1;
	}

	if(!EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv)) {
		log_error("EVP_DecryptInit_ex() failure");
		ERR_print_errors_fp(stderr);
		return -1;
	}

	// Set the expected tag value for authenticated data
	if(!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, cipher_password+tag_offset)) {
		log_error("EVP_CIPHER_CTX_ctrl() failure");
		ERR_print_errors_fp(stderr);
		return -1;
	}

	/* Not useful since len_iv = 12
	if(!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, len_iv, NULL)) {
		log_error("EVP_CIPHER_CTX_ctrl() failure");
		ERR_print_errors_fp(stderr);
		return -1;
	}	
	*/

	if(!EVP_DecryptUpdate(ctx, *plaintext_password, &len, cipher_password, tag_offset)) {
		log_error("EVP_DecryptUpdate() failure");
		ERR_print_errors_fp(stderr);
		return -1;
	}
	
	plaintext_len = len;

	if(1!=EVP_DecryptFinal_ex(ctx, *plaintext_password+len, &len)) {
		log_error("EVP_DecryptFinal_ex() failure");
		ERR_print_errors_fp(stderr);
	}

	plaintext_len += len;
	(*plaintext_password)[plaintext_len] = '\0';
	EVP_CIPHER_CTX_free(ctx);

	return 1;
}

/**
 * Get the Chromium base64 masterkey
 *
 * @return 1 on success, -1 on failure 
 */
int get_json_base64_key(char **b64_key) {
	char *home = getenv("LOCALAPPDATA");
	char local_state_path[MAX_PATH_SIZE];
	snprintf(local_state_path, MAX_PATH_SIZE, "%s\\Chromium\\User Data\\Local State", home);

	char *json;
	if(parse_json(local_state_path, &json) == -1) {
		log_error("parse_json failed()");
		return -1;
	}

	// FIXME: cJSON_Parse causes SEGFault, try to fix that.
	cJSON* values = cJSON_Parse(json);
	if(values == NULL) {
		free(json);
		log_error("cJSON_Parse() failed");
		fflush(stderr);
		return -1;
	}

	cJSON *os_crypt = NULL;
	cJSON *b64_encrypted_key = NULL;

	os_crypt = cJSON_GetObjectItemCaseSensitive(values, "os_crypt");	
	b64_encrypted_key = cJSON_GetObjectItemCaseSensitive(os_crypt, "encrypted_key");

	*b64_key = malloc(strlen(b64_encrypted_key->valuestring)+1); 
	if(*b64_key == NULL) {
		free(json);
		free(*b64_key);
		log_error("malloc() failure");
		return -1;
	}
	size_t b64_encrypted_key_len = strlen(b64_encrypted_key->valuestring);
	safe_strcpy(*b64_key, b64_encrypted_key->valuestring, b64_encrypted_key_len);
	free(json);

	return 1;
}



/**
 * Retrieve the base64 masterkey and DPAPI decrypt it
 *
 * @return 1 on success, -1 on failure 
 */
int get_base64_dpapi_key(char **key, int *key_len) {
	// 1 - Get the base64 os_crypt.encrypted_key
	char *base64_key;
	if(get_json_base64_key(&base64_key) == -1) {
		log_error("get_base64_key() error");
		return -1;
	}

	if(base64_decode(base64_key, key, key_len) == -1) {
		free(base64_key);
		log_error("base64_decode() failure");
		return -1;
	}
	free(base64_key);

	return 1;
}

/**
 * Main function to decrypt any chrome encrypted data 
 * 
 * @return 1 on success, -1 on failure 
 */
int decrypt_chrome_cipher(char *cipher_password, int len_cipher_password, char **plaintext_password, char *masterkey) {
	// TODO: find a better way to check for this
	if(cipher_password[0] == 'v' && cipher_password[1] == '1') {
		// We strip out the version number ("V10")
		cipher_password = &(cipher_password[3]);
		len_cipher_password -= 3;

		int len_iv = 96 / 8;
		char iv[len_iv+1];
		memcpy(iv, cipher_password, len_iv);
		iv[len_iv] = '\0';
		// We strip out the IV value (12 bytes);
		cipher_password = &(cipher_password[len_iv]);
		len_cipher_password -= len_iv;

		// 3 - Decrypting the cipher_password
		if(aead_decrypt(cipher_password, len_cipher_password, masterkey, iv, len_iv, plaintext_password) == -1) {
			log_error("aead_decrypt failure");
			return -1;
		}
	}
	else {
		if(dpapi_decrypt(cipher_password, len_cipher_password, plaintext_password) == -1) {
			log_error("dpapi_decrypt() failure");
			return -1;
		}
	}

	return 1;
}

/**
 * Get the masterkey if it exists or not
 *
 * @return 1 on success, -1 on failure 
 */
int get_masterkey(char *login_data_path, char **masterkey) {
	if(strstr(login_data_path, "Chromium")) {
		char *dpapi_key;
		int dpapi_key_len;
		// 1 - Getting the base64 decoded DPAPI key
		//if(PKCS5_PBKDF2_HMAC("peanuts", strlen("peanuts"), "salt", strlen("salt"), 1, EVP_sha256(), KEY_LENGTH, hkdf_key) == 0) {
		if(get_base64_dpapi_key(&dpapi_key, &dpapi_key_len) == -1) {
			log_error("get_base64_dpapi_key() failure");
			return -1;
		}
		// We strip out the "DPAPI" part
		dpapi_key = &(dpapi_key[5]);
		dpapi_key_len -= 5;
		// 2 - Decrypting DPAPI key
		if(dpapi_decrypt(dpapi_key, dpapi_key_len, masterkey) == -1) {
			log_error("dpapi_decrypt() failure");
			return -1;
		}
		free(dpapi_key);
	}
	return 1;
}

/**
 * Load the Windows Chrome Paths
 *
 * @return 1 on success, -1 on failure 
 */
int load_chrome_paths(char *chrome_path, char *chrome_login_data_path, char *chromium_path, char *chromium_login_data_path) {
	char *home = getenv("LOCALAPPDATA");
	snprintf(chrome_path, MAX_PATH_SIZE, "%s\\Google\\Chrome\\User Data\\Default", home);
	snprintf(chrome_login_data_path, MAX_PATH_SIZE, "%s\\Login Data", chrome_path);
	snprintf(chromium_path, MAX_PATH_SIZE, "%s\\Chromium\\User Data\\Default", home);
	snprintf(chromium_login_data_path, MAX_PATH_SIZE, "%s\\Login Data", chromium_path);
	//snprintf(brave_path, MAX_PATH_SIZE, "%s/.config/BraveSoftware/Brave-Browser/Default", home);
	//snprintf(brave_login_data_path, "%s/Login Data", brave_path);
	
	return 1;
}

/*
// Useless except if is g_use_mock_key is active (only for testing so probably never)
int get_hkdf_key(char **hkdf_key, int len_hkdf_key) {
	EVP_PKEY_CTX *pctx;
	unsigned char out[len_hkdf_key];
	size_t outlen = sizeof(out);
	pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
	if (EVP_PKEY_derive_init(pctx) <= 0) {
		log_error("EVP_PKEY_derive_init() failure");
		return -1;
	}
	if (EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0) {
		log_error("EVP_PKEY_CTX_set_hkdf_md() failure");
		return -1;
	}
	if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, "salt", 4) <= 0) {
		log_error("EVP_PKEY_CTX_set1_salt() failure");
		return -1;
	}
	if (EVP_PKEY_CTX_set1_hkdf_key(pctx, "peanuts", 7) <= 0) {
		log_error("EVP_PKEY_CTX_set1_key() failure");
		return -1;
	}
	if (EVP_PKEY_CTX_add1_hkdf_info(pctx, "info", 4) <= 0) {
		log_error("EVP_PKEY_CTX_add1_hkdf_info() failure");
		return -1;
	}
	if (EVP_PKEY_derive(pctx, out, &outlen) <= 0) {
		log_error("EVP_PKEY_derive() failure");
		return -1;
	}
	memcpy(hkdf_key, out, 32);

	return 1;
}
*/


