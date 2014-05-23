#include <jni.h>
#include <jvmti.h>

#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <unistd.h>

#define PRINT_CAPABABILITY(Y) printf("%s : %d\n", #Y, capabilities.Y)
#define PRINT_ERROR_CODE(X) printf("Error Code : %d", (int)X)

FILE *method_file = NULL;
int verbose = 0;

void open_file() {
    char methodFileName[500];
    sprintf(methodFileName, "/tmp/perf-%d.map", getpid());
    method_file = fopen(methodFileName, "w");
}

void JNICALL
cbVMInit(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread) {
    if (!method_file)
        open_file();
}

static void JNICALL
cbVMStart(jvmtiEnv *jvmti, JNIEnv *env) {
    jvmtiJlocationFormat format;
    (*jvmti)->GetJLocationFormat(jvmti, &format);

if(verbose>0)
    printf("[tracker] VMStart LocationFormat: %d\n", format);

}

static void JNICALL
cbCompiledMethodLoad(jvmtiEnv *env,
            jmethodID method,
            jint code_size,
            const void* code_addr,
            jint map_length,
            const jvmtiAddrLocationMap* map,
            const void* compile_info) 
{
    int i;

    char *name;
    char *msig;
    (*env)->GetMethodName(env, method, &name, &msig, NULL);

    jclass class;
    (*env)->GetMethodDeclaringClass(env, method, &class);
    char *csig;
    (*env)->GetClassSignature(env, class, &csig, NULL);

    char* source_file;

    fprintf(method_file, "%lx %x %s.%s%s\n", (long unsigned int)code_addr, code_size, csig, name, msig);
    fsync(fileno(method_file));
    (*env)->Deallocate(env, (unsigned char *)name);
    (*env)->Deallocate(env, (unsigned char *)msig);
    (*env)->Deallocate(env, (unsigned char *)csig);

    if(verbose>1)
        for (i = 0; i < map_length; i++) 
        {
          printf("[tracker] Entry: start_address: 0x%lx location: %d\n", 
                 (unsigned long int)map[i].start_address, 
                 (int)map[i].location);
        }
}

void JNICALL
cbDynamicCodeGenerated(jvmtiEnv *jvmti_env,
            const char* name,
            const void* address,
            jint length) 
{
    if (!method_file)
        open_file();

    fprintf(method_file, "%lx %x %s\n", (long unsigned int)address, length, name);

    if(verbose>1)
        printf("[tracker] Code generated: %s %lx %x\n", 
                name, 
                (unsigned long int)address, 
                length);
}

void JNICALL
cbCompiledMethodUnload(jvmtiEnv *jvmti_env,
            jmethodID method,
            const void* code_addr) 
{
    if(verbose>1)
        printf("[tracker] Unloaded %ld code_addr: 0x%lx\n", 
               (long int)method, 
               (unsigned long int)code_addr);
}

JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved) {
    jvmtiEnv              *jvmti;
    jvmtiError             error;
    jint                   res;
    jvmtiCapabilities      capabilities;
    jvmtiEventCallbacks    callbacks;

    if(options)
    { 
        if(0 == strcmp(options, "vv"))
        {
            verbose = 2;
        }
        else if(0 == strcmp(options, "v"))
        {
            verbose = 1;
        }
        else
            verbose = 0;
    }
    // Create the JVM TI environment (jvmti).
    res = (*vm)->GetEnv(vm, (void **)&jvmti, JVMTI_VERSION_1);
    if(!res == JNI_OK)
    {
        printf("An error occured while retrieving the current environment");
        return JNI_ERR;
    }

    // Clear the capabilities structure and set the ones you need.
    memset(&capabilities,0, sizeof(capabilities));
    capabilities.can_generate_all_class_hook_events  = 1;
    capabilities.can_tag_objects                     = 1;
    capabilities.can_generate_object_free_events     = 1;
    capabilities.can_get_source_file_name            = 1;
    capabilities.can_get_line_numbers                = 1;
    capabilities.can_generate_vm_object_alloc_events = 1;
    capabilities.can_generate_compiled_method_load_events = 1;

    // Request these capabilities for this JVM TI environment.
    error = (*jvmti)->AddCapabilities(jvmti, &capabilities);
    if(verbose>0)
    {
        PRINT_CAPABABILITY(can_generate_all_class_hook_events);
        PRINT_CAPABABILITY(can_tag_objects);
        PRINT_CAPABABILITY(can_generate_object_free_events);
        PRINT_CAPABABILITY(can_get_source_file_name);
        PRINT_CAPABABILITY(can_get_line_numbers);
        PRINT_CAPABABILITY(can_generate_vm_object_alloc_events);
        PRINT_CAPABABILITY(can_generate_compiled_method_load_events);
    }

    if(!error == JVMTI_ERROR_NONE)
    {
        printf("An error occured during the capabilities retrieval\n");
        PRINT_ERROR_CODE(error);
        return JNI_ERR;
    }

    // Clear the callbacks structure and set the ones you want.
    memset(&callbacks,0, sizeof(callbacks));
    callbacks.VMInit           = &cbVMInit;
    callbacks.VMStart           = &cbVMStart;
    callbacks.CompiledMethodLoad  = &cbCompiledMethodLoad;
    callbacks.CompiledMethodUnload  = &cbCompiledMethodUnload;
    callbacks.DynamicCodeGenerated = &cbDynamicCodeGenerated;
    error = (*jvmti)->SetEventCallbacks(jvmti, &callbacks,
                      (jint)sizeof(callbacks));

    if(!error == JVMTI_ERROR_NONE)
    {
        printf("An error occurred while assigning the callbacks\n");
        PRINT_ERROR_CODE(error);
        return JNI_ERR;
    }

    //  If error!=JVMTI_ERROR_NONE, the callbacks were not accepted.

    // For each of the above callbacks, enable this event.
    error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                      JVMTI_EVENT_VM_INIT, (jthread)NULL);

    if(!error == JVMTI_ERROR_NONE)
    {
        printf("An error occurred while enabling JVMTI_EVENT_VM_INIT\n");
        PRINT_ERROR_CODE(error);
        return JNI_ERR;
    }

    error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                      JVMTI_EVENT_VM_START, (jthread)NULL);
    if(!error == JVMTI_ERROR_NONE)
    {
        printf("An error occurred while enabling JVMTI_EVENT_VM_START\n");
        PRINT_ERROR_CODE(error);
        return JNI_ERR;
    }

    error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                      JVMTI_EVENT_COMPILED_METHOD_LOAD, (jthread)NULL);
    if(!error == JVMTI_ERROR_NONE)
    {
        printf("An error occurred while enabling JVMTI_EVENT_COMPILED_METHOD_LOAD\n");
        PRINT_ERROR_CODE(error);
        return JNI_ERR;
    }

    error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                      JVMTI_EVENT_COMPILED_METHOD_UNLOAD, (jthread)NULL);
    if(!error == JVMTI_ERROR_NONE)
    {
        printf("An error occurred while enabling JVMTI_EVENT_COMPILED_METHOD_UNLOAD\n");
        PRINT_ERROR_CODE(error);
        return JNI_ERR;
    }
    
    error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                      JVMTI_EVENT_DYNAMIC_CODE_GENERATED, (jthread)NULL);
    if(!error == JVMTI_ERROR_NONE)
    {
        printf("An error occurred while enabling JVMTI_EVENT_DYNAMIC_CODE_GENERATED\n");
        PRINT_ERROR_CODE(error);
        return JNI_ERR;
    }

    return JNI_OK; 
}


