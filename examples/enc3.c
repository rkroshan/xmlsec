/** 
 * XML Security Library example: Encrypting XML file with a session key and dynamicaly created template.
 * 
 * Encrypts XML file using a dynamicaly created template file and a session 
 * DES key (encrypted with an RSA key).
 * 
 * Usage: 
 *	./enc3 <xml-doc> <rsa-pem-key-file> 
 *
 * Example:
 *	./enc3 enc3-doc.xml rsakey.pem > enc3-res.xml
 *
 * The result could be decrypted with enc6 example:
 *	./enc6 enc3-res.xml
 *
 * This is free software; see Copyright file in the source
 * distribution for preciese wording.
 * 
 * Copyrigth (C) 2002-2003 Aleksey Sanin <aleksey@aleksey.com>
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#ifndef XMLSEC_NO_XSLT
#include <libxslt/xslt.h>
#endif /* XMLSEC_NO_XSLT */

#include <xmlsec/xmlsec.h>
#include <xmlsec/xmltree.h>
#include <xmlsec/xmlenc.h>
#include <xmlsec/templates.h>
#include <xmlsec/crypto.h>

xmlSecKeysMngrPtr load_rsa_keys(char** files, int files_size);
int encrypt_file(xmlSecKeysMngrPtr mngr, const char* xml_file);

int 
main(int argc, char **argv) {
    xmlSecKeysMngrPtr mngr;
    
    assert(argv);

    if(argc != 3) {
	fprintf(stderr, "Error: wrong number of arguments.\n");
	fprintf(stderr, "Usage: %s <xml-file> <key-file>\n", argv[0]);
	return(1);
    }

    /* Init libxml and libxslt libraries */
    xmlInitParser();
    LIBXML_TEST_VERSION
    xmlLoadExtDtdDefaultValue = XML_DETECT_IDS | XML_COMPLETE_ATTRS;
    xmlSubstituteEntitiesDefault(1);
#ifndef XMLSEC_NO_XSLT
    xmlIndentTreeOutput = 1; 
#endif /* XMLSEC_NO_XSLT */
        	
    /* Init xmlsec library */
    if(xmlSecInit() < 0) {
	fprintf(stderr, "Error: xmlsec initialization failed.\n");
	return(-1);
    }

    /* Init crypto library */
    if(xmlSecCryptoAppInit(NULL) < 0) {
	fprintf(stderr, "Error: crypto initialization failed.\n");
	return(-1);
    }

    /* Init xmlsec-crypto library */
    if(xmlSecCryptoInit() < 0) {
	fprintf(stderr, "Error: xmlsec-crypto initialization failed.\n");
	return(-1);
    }

    /* create keys manager and load keys */
    mngr = load_rsa_keys(&(argv[2]), argc - 2);
    if(mngr == NULL) {
	return(-1);
    }

    if(encrypt_file(mngr, argv[1]) < 0) {
	xmlSecKeysMngrDestroy(mngr);
	return(-1);
    }    

    /* destroy keys manager */
    xmlSecKeysMngrDestroy(mngr);
    
    /* Shutdown xmlsec-crypto library */
    xmlSecCryptoShutdown();
    
    /* Shutdown crypto library */
    xmlSecCryptoAppShutdown();
    
    /* Shutdown xmlsec library */
    xmlSecShutdown();

    /* Shutdown libxslt/libxml */
#ifndef XMLSEC_NO_XSLT
    xsltCleanupGlobals();            
#endif /* XMLSEC_NO_XSLT */
    xmlCleanupParser();
    
    return(0);
}

/**
 * load_rsa_keys:
 * @files:		the list of filenames.
 * @files_size:		the number of filenames in #files.
 *
 * Creates simple keys manager and load RSA keys from #files in it.
 * The caller is responsible for destroing returned keys manager using
 * @xmlSecKeysMngrDestroy.
 *
 * Returns the pointer to newly created keys manager or NULL if an error
 * occurs.
 */
xmlSecKeysMngrPtr 
load_rsa_keys(char** files, int files_size) {
    xmlSecKeysMngrPtr mngr;
    xmlSecKeyPtr key;
    int i;
    
    assert(files);
    assert(files_size > 0);
    
    /* create and initialize keys manager, we use a simple list based
     * keys manager, implement your own xmlSecKeysStore klass if you need
     * something more sophisticated 
     */
    mngr = xmlSecKeysMngrCreate();
    if(mngr == NULL) {
	fprintf(stderr, "Error: failed to create keys manager.\n");
	return(NULL);
    }
    if(xmlSecCryptoAppSimpleKeysMngrInit(mngr) < 0) {
	fprintf(stderr, "Error: failed to initialize keys manager.\n");
	xmlSecKeysMngrDestroy(mngr);
	return(NULL);
    }    
    
    for(i = 0; i < files_size; ++i) {
	assert(files[i]);

	/* load private RSA key */
	key = xmlSecCryptoAppPemKeyLoad(files[i], NULL, NULL, 1);
	if(key == NULL) {
    	    fprintf(stderr,"Error: failed to load rsa key from file \"%s\"\n", files[i]);
	    xmlSecKeysMngrDestroy(mngr);
	    return(NULL);
	}

	/* set key name to the file name, this is just an example! */
	if(xmlSecKeySetName(key, BAD_CAST files[i]) < 0) {
    	    fprintf(stderr,"Error: failed to set key name for key from \"%s\"\n", files[i]);
	    xmlSecKeyDestroy(key);
	    xmlSecKeysMngrDestroy(mngr);
	    return(NULL);
	}
	
	/* add key to keys manager, from now on keys manager is responsible 
	 * for destroying key 
	 */
	if(xmlSecCryptoAppSimpleKeysMngrAdoptKey(mngr, key) < 0) {
    	    fprintf(stderr,"Error: failed to add key from \"%s\" to keys manager\n", files[i]);
	    xmlSecKeyDestroy(key);
	    xmlSecKeysMngrDestroy(mngr);
	    return(NULL);
	}
    }

    return(mngr);
}

/**
 * encrypt_file:
 * @mngr:		the pointer to keys manager.
 * @xml_file:		the encryption template file name.
 *
 * Encrypts #xml_file using a dynamicaly created template, a session DES key 
 * and an RSA key from keys manager.
 *
 * Returns 0 on success or a negative value if an error occurs.
 */
int 
encrypt_file(xmlSecKeysMngrPtr mngr, const char* xml_file) {
    xmlDocPtr doc = NULL;
    xmlNodePtr encDataNode = NULL;
    xmlNodePtr keyInfoNode = NULL;
    xmlNodePtr encKeyNode = NULL;
    xmlNodePtr keyInfoNode2 = NULL;
    xmlSecEncCtxPtr encCtx = NULL;
    int res = -1;
    
    assert(mngr);
    assert(xml_file);

    /* load template */
    doc = xmlParseFile(xml_file);
    if ((doc == NULL) || (xmlDocGetRootElement(doc) == NULL)){
	fprintf(stderr, "Error: unable to parse file \"%s\"\n", xml_file);
	goto done;	
    }
    
    /* create encryption template to encrypt XML file and replace 
     * its content with encryption result */
    encDataNode = xmlSecTmplEncDataCreate(doc, xmlSecTransformDes3CbcId,
				NULL, xmlSecTypeEncElement, NULL, NULL);
    if(encDataNode == NULL) {
	fprintf(stderr, "Error: failed to create encryption template\n");
	goto done;   
    }

    /* we want to put encrypted data in the <enc:CipherValue/> node */
    if(xmlSecTmplEncDataEnsureCipherValue(encDataNode) == NULL) {
	fprintf(stderr, "Error: failed to add CipherValue node\n");
	goto done;   
    }

    /* add <dsig:KeyInfo/> */
    keyInfoNode = xmlSecTmplEncDataEnsureKeyInfo(encDataNode, NULL);
    if(keyInfoNode == NULL) {
	fprintf(stderr, "Error: failed to add key info\n");
	goto done;		
    }

    /* add <enc:EncryptedKey/> to store the encrypted session key */
    encKeyNode = xmlSecTmplKeyInfoAddEncryptedKey(keyInfoNode, 
				    xmlSecTransformRsaOaepId, 
				    NULL, NULL, NULL);
    if(encKeyNode == NULL) {
	fprintf(stderr, "Error: failed to add key info\n");
	goto done;		
    }

    /* we want to put encrypted key in the <enc:CipherValue/> node */
    if(xmlSecTmplEncDataEnsureCipherValue(encKeyNode) == NULL) {
	fprintf(stderr, "Error: failed to add CipherValue node\n");
	goto done;   
    }

    /* add <dsig:KeyInfo/> and <dsig:KeyName/> nodes to <enc:EncryptedKey/> */
    keyInfoNode2 = xmlSecTmplEncDataEnsureKeyInfo(encDataNode, NULL);
    if(keyInfoNode2 == NULL) {
	fprintf(stderr, "Error: failed to add key info\n");
	goto done;		
    }

    if(xmlSecTmplKeyInfoAddKeyName(keyInfoNode2, NULL) == NULL) {
	fprintf(stderr, "Error: failed to add key name\n");
	goto done;		
    }

    /* create encryption context */
    encCtx = xmlSecEncCtxCreate(mngr);
    if(encCtx == NULL) {
        fprintf(stderr,"Error: failed to create encryption context\n");
	goto done;
    }

    /* generate a Triple DES key */
    encCtx->encKey = xmlSecKeyGenerate(xmlSecKeyDataDesId, 192, xmlSecKeyDataTypeSession);
    if(encCtx->encKey == NULL) {
        fprintf(stderr,"Error: failed to generate session des key\n");
	goto done;
    }

    /* encrypt the data */
    if(xmlSecEncCtxXmlEncrypt(encCtx, encDataNode, xmlDocGetRootElement(doc)) < 0) {
        fprintf(stderr,"Error: encryption failed\n");
	goto done;
    }
    
    /* we template is inserted in the doc */
    encDataNode = NULL;
        
    /* print encrypted data with document to stdout */
    xmlDocDump(stdout, doc);
    
    /* success */
    res = 0;

done:    

    /* cleanup */
    if(encCtx != NULL) {
	xmlSecEncCtxDestroy(encCtx);
    }

    if(encDataNode != NULL) {
	xmlFreeNode(encDataNode);
    }
        
    if(doc != NULL) {
	xmlFreeDoc(doc); 
    }
    return(res);
}

