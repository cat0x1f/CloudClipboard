<template>
    <v-app>
       
        <v-app-bar
            app
            color="primary"
            dark
        >
            <v-toolbar-title>云剪贴板</v-toolbar-title>
            <v-spacer></v-spacer>
            <v-tooltip left>
                <template v-slot:activator="{ on }">
                    <v-btn icon v-on="on" @click="if (!$root.websocket && !$root.websocketConnecting) {$root.retry = 0; $root.connect();}">
                        <v-icon v-if="$root.websocket">{{mdiLanConnect}}</v-icon>
                        <v-icon v-else-if="$root.websocketConnecting">{{mdiLanPending}}</v-icon>
                        <v-icon v-else>{{mdiLanDisconnect}}</v-icon>
                    </v-btn>
                </template>
                <span v-if="$root.websocket">已连接</span>
                <span v-else-if="$root.websocketConnecting">连接中</span>
                <span v-else>未连接，点击重连</span>
            </v-tooltip>
        </v-app-bar>

        <v-main>
            <template v-if="$route.meta.keepAlive">
                <keep-alive><router-view /></keep-alive>
            </template>
            <router-view v-else />
        </v-main>

    </v-app>
</template>

<script>
import {
    mdiContentPaste,
    mdiDevices,
    mdiInformation,
    mdiLanConnect,
    mdiLanDisconnect,
    mdiLanPending,
    mdiBrightness4,
    mdiBulletinBoard,
    mdiDiceMultiple,
} from '@mdi/js';

export default {
    data() {
        return {
            drawer: false,
            mdiContentPaste,
            mdiDevices,
            mdiInformation,
            mdiLanConnect,
            mdiLanDisconnect,
            mdiLanPending,
            mdiBrightness4,
            mdiBulletinBoard,
            mdiDiceMultiple,
            navigator,
        };
    },
};
</script>
