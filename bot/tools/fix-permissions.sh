#!/bin/bash

echo "🔧 Configurando permissões do Firebase Functions..."

# Permite acesso público ao webhook do Telegram
gcloud functions add-iam-policy-binding telegramBot \
  --region=us-central1 \
  --member="allUsers" \
  --role="roles/cloudfunctions.invoker" \
  --project=botaprarodar-routes

echo "✅ Permissões configuradas!"