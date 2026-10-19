@ Appel système ajouté par Luma3DS (absent de libctru) : ControlService, svc 0xB0.
@ Result svcControlServiceLuma(u32 operation, Handle* sortie, const char* service)
@ Opération 0 (SERVICEOP_STEAL_CLIENT_SESSION) : copie de la connexion d'un autre processus à ce service.
@ N'appeler qu'après avoir vérifié que Luma3DS est là (lumaPresent dans sauvegardes.cpp).

    .arm
    .align 2
    .global svcControlServiceLuma
    .type svcControlServiceLuma, %function
svcControlServiceLuma:
    svc 0xB0
    bx lr
